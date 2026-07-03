#!/usr/bin/env python3
"""
SlimeVR serial dongle -> host bridge.

ESP devices that are not the S2 or S3 type have no USB peripheral, 
so unlike the ESP32 dongle it cannot present itself as a USB HID device. 
Instead it streams the exact same 64-byte "HID" transfers over its 
UART (the USB-serial bridge), framed so this program can recover them from
a stream that also carries human-readable debug text and the 
[SC] management protocol.

This script:
  1. reads the framed tracker transfers from the serial port,
  2. validates each frame's CRC (and resynchronises on corruption),
  3. forwards each recovered 64-byte transfer to the SlimeVR side over a
     localhost socket (UDP by default), and
  4. optionally echoes the dongle's debug text so you can still see logs.

The data is communicated over serial at a baud of 921600 for expidited data transfer.
Do note that this program is very IO heavy as it checks for data every 1 ms for latency.

Wire framing produced by the firmware (see src/transport/SerialFrameSink.cpp):

    0xA5 0x5A  <len:u8>  <payload[len]>  <crc8>

    crc8 = CRC-8 (poly 0x07, init 0x00) over (len byte + payload).

Each payload is one 64-byte HID transfer == four 16-byte reports. 
The default forwarding sends one 64-byte payload per UDP datagram to 
127.0.0.1:6969.

Requires: pyserial  (pip install pyserial)
"""

import argparse
import math
import socket
import struct
import sys
import time

try:
    import serial  # pyserial
    from serial.tools import list_ports
except ImportError:
    sys.exit("pyserial is required: pip install pyserial")

MAGIC0 = 0xA5
MAGIC1 = 0x5A
MAX_PAYLOAD = 250

# Precomputed CRC-8 table (poly 0x07, init 0x00) — must match the firmware.
# This helps speed up CRC computation.
def make_crc_table():
    table = []
    for i in range(256):
        crc = i
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
        table.append(crc)
    return table

_CRC_TABLE = make_crc_table()


def crc8(data: bytes) -> int:
    crc = 0x00
    for b in data:
        crc = _CRC_TABLE[crc ^ b]
    return crc


class FrameParser:
    """Incremental parser that pulls framed payloads out of a mixed byte stream.

    Feed it raw bytes; it yields (payload_bytes) for every valid frame and
    returns any non-frame bytes as "debug" so the caller can print logs.
    """

    def __init__(self):
        self.buf = bytearray()

    def feed(self, chunk: bytes):
        """Yield ('frame', payload) and ('text', bytes) tuples in stream order."""
        self.buf.extend(chunk)
        while True:
            # Find the start of a candidate frame
            idx = self.buf.find(bytes((MAGIC0, MAGIC1)))
            if idx == -1:
                # No magic present in the buffer. 
                # There could be a 0xA5 at the end of the buffer, so if it exists,
                # keep it; this would be the start of the frame
                keep = 1 if self.buf and self.buf[-1] == MAGIC0 else 0
                if len(self.buf) > keep:
                    yield ("text", bytes(self.buf[:len(self.buf) - keep]))
                    del self.buf[:len(self.buf) - keep]
                return

            # Anything before the magic is debug text
            if idx > 0:
                yield ("text", bytes(self.buf[:idx]))
                del self.buf[:idx]

            # The buffer needs the two magic chars and the length to process
            if len(self.buf) < 3:
                return
            
            length = self.buf[2]
            if length == 0 or length > MAX_PAYLOAD:
                # Length is invalid, meaning magic character is a false positive
                # Drop the first magic byte and try again
                yield ("text", bytes(self.buf[:1]))
                del self.buf[:1]
                continue

            # Get the length of the entire frame
            # including magic, length, payload, and crc
            frame_len = 3 + length + 1

            # If there isn't enough data in the buffer, wait for more
            if len(self.buf) < frame_len:
                return

            # Form the payload and CRC
            payload = bytes(self.buf[3:3 + length])
            crc = self.buf[3 + length]

            # Verify CRC before returning the full payload
            if crc8(bytes((length,)) + payload) == crc:
                yield ("frame", payload)
                del self.buf[:frame_len]
            else:
                # Bad CRC -> false positive. Drop first magic byte and resync.
                yield ("text", bytes(self.buf[:1]))
                del self.buf[:1]


class SlimeVRForwarder:
    """Translates the dongle's 16-byte HID reports into the SlimeVR UDP tracker
    protocol so each tracker shows up as a network tracker on the server.

    A tracker connects to the UDP socket that runs on the server. Currently, we just
    format the data sent from the dongle into a format that the server recognizes via
    UDP. The format that SlimeVR uses for HID is very different compared to UDP transmit,
    so we need to do some translation.
    
    NOTE: Protocol 8 is used here to get the sensor data recognized by the server. Future
    development of the dongle on serial should adopt the newer protocol, which allows for
    setting the magnetometer settings. For now, the older protocol is good enough to set up.
    """

    # Explicitly choose protocol 8 to get data to register on the server immediately
    # TODO: Need the tracker / receiver to handle type 15 packets taht send sensor info
    PROTOCOL_VERSION = 8
    FW_STRING = b"SlimeVR-ESPNow-Bridge"

    # SlimeVR UDP packet ids
    PKT_HANDSHAKE = 3
    PKT_ACCEL = 4
    PKT_BATTERY = 12
    PKT_ROTATION_DATA = 17
    PKT_TEMPERATURE = 20

    # Battery/temperature change slowly; don't send them faster than this
    INFO_SECONDS = 1.0

    # Wait up to this long for the tracker's identity — the real MAC (from the
    # dongle's registration report) and the device-info report (board / MCU / IMU
    # / firmware) — before handshaking with whatever we have. board/MCU can only
    # be set on the FIRST handshake server-side, so it's worth a brief wait.
    HANDSHAKE_WAIT_SECONDS = 2.0

    # Resend the last rotation at least this often so a still tracker (which stops
    # sending) doesn't get registered as "timed out" by the server.
    # The HID path never times out but the UDP path does, 
    # so we emulate a continuous stream.
    KEEPALIVE_SECONDS = 1

    def __init__(self, host="127.0.0.1", port=6969, verbose=False):
        self.host = host
        self.port = port
        self.verbose = verbose
        self.conns = {}  # trackerId -> connection state

    def _conn(self, tid):
        c = self.conns.get(tid)
        if c is None:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect((self.host, self.port))  # fixes a source port per tracker
            c = {
                "sock": s, "seq": 1, "mac": None, "handshook": False,
                "board": 0, "mcu": 0, "imu": 0, "fw": None,     # from device-info (type 0)
                "have_info": False,                             # device-info seen yet?
                "first_seen": time.monotonic(),                 # when we first saw this tracker
                "last_rot": None,                               # last quaternion sent (for keepalive)
                "last_send": 0.0,                               # monotonic time of last packet
                "last_info": 0.0,                               # monotonic time of last battery/temp
            }
            self.conns[tid] = c
        return c

    def _drop(self, tid):
        c = self.conns.pop(tid, None)
        if c is not None:
            try:
                c["sock"].close()
            except OSError:
                pass
            if self.verbose:
                print(f"[slimevr] tracker {tid} disconnected", file=sys.stderr)

    def _send(self, c, packet_id, payload):
        # header: int32 packetId + int64 packetNumber, both big-endian
        num = c["seq"] if c["handshook"] else 0
        try:
            c["sock"].send(struct.pack(">iq", packet_id, num) + payload)
        except ConnectionRefusedError:
            print("[ERROR] SlimeVR seems to have gone down... Restart SlimeVR first and then run this program again.")
            exit(1)
        if c["handshook"]:
            c["seq"] += 1

    def _handshake(self, c, tid):
        mac = c["mac"] or bytes((0x02, 0, 0, 0, 0, tid & 0xFF))
        # The board/MCU/IMU ids from the tracker's device-info report are the same
        # ids the server's BoardType/MCUType/IMUType.getById() expect. firmware
        # must be non-empty or else the server treats us as legacy owoTrack....
        fw = c["fw"] or self.FW_STRING
        payload = struct.pack(">iii", c["board"], c["imu"], c["mcu"])
        payload += struct.pack(">iii", 0, 0, 0)         # IMU info (3 ints) — n/a
        payload += struct.pack(">i", self.PROTOCOL_VERSION)
        payload += bytes((len(fw),)) + fw
        payload += mac
        for _ in range(3):  # localhost shouldn't lose packets, but just in case...
            self._send(c, self.PKT_HANDSHAKE, payload)
        c["handshook"] = True
        if self.verbose:
            macs = ":".join("%02X" % b for b in mac)
            print(f"[slimevr] handshake tracker {tid} mac={macs} board={c['board']} "
                  f"mcu={c['mcu']} imu={c['imu']} fw={fw.decode('ascii', 'replace')}",
                  file=sys.stderr)

    def _ready(self, c, tid):
        """Handshake once we have the tracker's identity (real MAC + device-info),
        or after a fallback so it still appears if either is slow. Returns True
        when the connection is handshook and ready for data."""
        if c["handshook"]:
            return True
        have_identity = c["mac"] is not None and c["have_info"]
        if have_identity or (time.monotonic() - c["first_seen"]) >= self.HANDSHAKE_WAIT_SECONDS:
            self._handshake(c, tid)
            return True
        return False  # still waiting for MAC / device-info

    def _send_rotation(self, c, qx, qy, qz, qw):
        # packet 17: sensorId(1) dataType(1=normal) quat(x,y,z,w f32 BE) calib(1)
        c["last_rot"] = (qx, qy, qz, qw)
        c["last_send"] = time.monotonic()
        self._send(c, self.PKT_ROTATION_DATA,
                   bytes((0, 1)) + struct.pack(">ffff", qx, qy, qz, qw) + bytes((0,)))

    def keepalive(self):
        """Resend the last rotation for idle trackers so they don't time out.
        Call this periodically from the main loop."""
        now = time.monotonic()
        for c in self.conns.values():
            if c["handshook"] and c["last_rot"] is not None and \
                    (now - c["last_send"]) >= self.KEEPALIVE_SECONDS:
                self._send_rotation(c, *c["last_rot"])

    def _send_info(self, c, batt, batt_v, temp):
        # Battery + temperature from a device-info (type 0) or reduced (type 2)
        # report. Throttled since these change slowly. Scaling mirrors the
        # server's HID decode / UDP handling.
        now = time.monotonic()
        if (now - c["last_info"]) < self.INFO_SECONDS:
            return
        c["last_info"] = now
        # Battery (packet 12): voltage, level floats. Server does level * 100 and
        # treats < 0.01 as unknown (-1). HID level is a 0-100 percentage.
        level = 0.0 if batt == 128 else (batt & 0x7F) / 100.0
        voltage = (batt_v + 245) / 100.0
        self._send(c, self.PKT_BATTERY, struct.pack(">ff", voltage, level))
        # Temperature (packet 20): sensorId, temp float. HID: temp/2 - 39 C,
        # temp == 0 means unknown.
        if temp > 0:
            self._send(c, self.PKT_TEMPERATURE,
                       bytes((0,)) + struct.pack(">f", temp / 2.0 - 39.0))

    def _send_accel(self, c, ax, ay, az):
        # packet 4: accel(x,y,z f32 BE) sensorId(1).
        # Protocol < 22 => the server rotates accel by -90deg about Z
        # (SENSOR_OFFSET_CORRECTION). Pre-rotate by +90deg (x,y,z)->(-y,x,z) so
        # the stored acceleration ends up in the correct frame.
        self._send(c, self.PKT_ACCEL,
                   struct.pack(">fff", -ay, ax, az) + bytes((0,)))

    def __call__(self, transfer):
        # A transfer is four 16-byte reports (some may be zero padding).
        for off in range(0, len(transfer) - 15, 16):
            self._feed_report(transfer[off:off + 16])

    def _feed_report(self, r):
        # The firmware zero-pads unused report slots in each 64-byte transfer.
        # An all-zero report is padding (and also looks like "device info for
        # tracker 0"), so skip it to avoid spawning a phantom tracker.
        if not any(r):
            return

        ptype = r[0]
        tid = r[1]

        if ptype == 0xFF:  # dongle registration: [0xff][id][6-byte mac][...]
            c = self._conn(tid)
            if c["mac"] is None:
                c["mac"] = bytes(r[2:8])
            self._ready(c, tid)  # handshake now if device-info already arrived
            return

        if ptype == 0:  # device info: board b5, mcu b6, imu b8, fw b12-14,
            c = self._conn(tid)             # batt b2, batt_v b3, temp b4
            c["board"] = r[5]
            c["mcu"] = r[6]
            c["imu"] = r[8]
            c["fw"] = ("%d.%d.%d" % (r[12], r[13], r[14])).encode("ascii")
            c["have_info"] = True
            if self._ready(c, tid):
                self._send_info(c, r[2], r[3], r[4])
            return

        if ptype == 3:  # status: byte 2 == 0 => disconnected
            if r[2] == 0:
                self._drop(tid)
            return

        if ptype in (1, 4):  # full-precision quat (accel for 1, mag for 4)
            c = self._conn(tid)
            if not self._ready(c, tid):
                return  # hold data until we can handshake with the real MAC
            q = struct.unpack_from("<hhhh", r, 2)  # x,y,z,w as Q15
            s = 1.0 / 32768.0
            self._send_rotation(c, q[0] * s, q[1] * s, q[2] * s, q[3] * s)
            if ptype == 1:
                a = struct.unpack_from("<hhh", r, 10)  # accel Q7, m/s^2
                sa = 1.0 / 128.0
                self._send_accel(c, a[0] * sa, a[1] * sa, a[2] * sa)
            return

        if ptype in (2, 7):  # reduced-precision (exponential-map) quat + accel
            c = self._conn(tid)
            if not self._ready(c, tid):
                return
            qbuf = struct.unpack_from("<I", r, 5)[0]
            v0 = (qbuf & 1023) / 1024.0 * 2 - 1
            v1 = ((qbuf >> 10) & 2047) / 2048.0 * 2 - 1
            v2 = ((qbuf >> 21) & 2047) / 2048.0 * 2 - 1
            d = v0 * v0 + v1 * v1 + v2 * v2
            inv = 1.0 / math.sqrt(d + 1e-6)
            ang = (math.pi / 2) * d * inv
            k = math.sin(ang) * inv
            self._send_rotation(c, k * v0, k * v1, k * v2, math.cos(ang))
            a = struct.unpack_from("<hhh", r, 9)  # accel Q7, m/s^2
            sa = 1.0 / 128.0
            self._send_accel(c, a[0] * sa, a[1] * sa, a[2] * sa)
            if ptype == 2:  # type 2 carries batt/batt_v/temp (type 7 has button)
                self._send_info(c, r[2], r[3], r[4])
            return

        # types 3 (status), 5 (runtime), 6 (button) are not needed to appear.


def make_forwarder(args):
    """Return a callable(transfer: bytes) that ships each 64-byte transfer onward."""
    mode = args.forward
    if mode == "none":
        return lambda payload: None

    if mode == "slimevr":
        return SlimeVRForwarder(args.forward_host, args.forward_port,
                                verbose=args.print_debug)

    if mode == "udp":
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        dest = (args.forward_host, args.forward_port)
        return lambda payload: sock.sendto(payload, dest)

    if mode == "tcp-client":
        state = {"sock": None}

        def send_tcp(payload):
            if state["sock"] is None:
                try:
                    s = socket.create_connection(
                        (args.forward_host, args.forward_port), timeout=2)
                    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                    state["sock"] = s
                    print(f"[bridge] connected to "
                          f"{args.forward_host}:{args.forward_port}", file=sys.stderr)
                except OSError as e:
                    return  # server not up yet; drop this transfer
            try:
                state["sock"].sendall(payload)
            except OSError:
                try:
                    state["sock"].close()
                finally:
                    state["sock"] = None

        return send_tcp

    raise ValueError(f"unknown forward mode {mode}")


def port_parse(args):
    if args.port:
        return args.port
    ports = list(list_ports.comports())
    if not ports:
        sys.exit("[bridge] no serial ports found; specify --port")
    # Prefer a device whose VID/PID matches a CH340 USB-to-serial converter
    for p in ports:
        if (p.vid, p.pid) == (0x1a86, 0x7523):
            return p.device

    # Search for valid ports and ask user to select one
    print("[bridge] available ports:", file=sys.stderr)
    port_ind = 0
    ports_selection = []
    for p in ports:
        if p.description != "n/a":
            print(f"{port_ind}: {p.device}  {p.description}", file=sys.stderr)
            ports_selection.append(p.device)
    if not ports_selection:
        print("[bridge] no valid ports found, please connect your device", file=sys.stderr)
        exit(1)

    while True:
        try:
            port_ind = int(input("[bridge] select port: "))
            if port_ind < 0 or port_ind >= len(ports_selection):
                raise ValueError
            break
        except ValueError:
            print("[bridge] invalid port number", file=sys.stderr)

    return ports_selection[port_ind]


def main():
    ap = argparse.ArgumentParser(description="SlimeVR ESP8266 dongle serial bridge")
    ap.add_argument("--port", help="serial port (auto-detected if omitted)")
    ap.add_argument("--baud", type=int, default=921600,
                    help="serial baud (must match SLIME_SERIAL_BAUD, default 921600)")
    ap.add_argument("--forward", choices=["slimevr", "udp", "tcp-client", "none"],
                    default="slimevr",
                    help="slimevr: translate to the SlimeVR UDP tracker protocol "
                         "(default); udp/tcp-client: forward raw 64-byte transfers")
    ap.add_argument("--forward-host", default="127.0.0.1")
    ap.add_argument("--forward-port", type=int, default=6969,
                    help="SlimeVR server UDP port (default 6969)")
    ap.add_argument("--print-debug", action="store_true",
                    help="echo the dongle's debug/[SC] text to stdout")
    ap.add_argument("--hexdump", action="store_true",
                    help="print each recovered 64-byte transfer as hex")
    ap.add_argument("--stats-interval", type=float, default=2.0,
                    help="seconds between throughput stats (0 to disable)")
    
    # Parse arguments
    args = ap.parse_args()
    port = port_parse(args)

    print(f"[bridge] opening {port} @ {args.baud} baud, "
          f"forwarding via {args.forward} -> {args.forward_host}:{args.forward_port}",
          file=sys.stderr)

    forward = make_forwarder(args)
    parser = FrameParser()

    frames = 0
    payload_bytes = 0
    last_stats = time.monotonic()
    text_buf = bytearray()

    keepalive = getattr(forward, "keepalive", None)

    # IMPORTANT: Timeout on serial needs to be low (< 1ms) to retrieve data quickly
    with serial.Serial(port, args.baud, timeout=0.001) as ser:
        while True:
            chunk = ser.read(4096)
            # Resend last poses for idle trackers so they don't time out
            if keepalive is not None:
                keepalive()
            if chunk:
                for kind, data in parser.feed(chunk):
                    if kind == "frame":
                        frames += 1
                        payload_bytes += len(data)
                        forward(data)
                        if args.hexdump:
                            print(data.hex(" "))
                    else:  # text
                        if args.print_debug:
                            text_buf.extend(data)
                            while b"\n" in text_buf:
                                line, _, rest = text_buf.partition(b"\n")
                                text_buf[:] = rest
                                sys.stdout.write(
                                    line.decode("utf-8", "replace").rstrip("\r") + "\n")
                            sys.stdout.flush()

            if args.stats_interval > 0:
                now = time.monotonic()
                dt = now - last_stats
                if dt >= args.stats_interval:
                    bps = payload_bytes / dt
                    fps = frames / dt
                    print(f"[bridge] {fps:6.0f} transfers/s  {bps/1000:7.1f} KB/s payload",
                          file=sys.stderr)
                    frames = 0
                    payload_bytes = 0
                    last_stats = now


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
