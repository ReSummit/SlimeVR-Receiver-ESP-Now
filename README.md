# SlimeVR ESP-NOW Dongle

Alternative firmware for SlimeVR tracker dongles that uses the [ESP-NOW protocol](https://www.espressif.com/en/solutions/low-power-solutions/esp-now) instead of standard WiFi. ESP-NOW provides a direct device-to-device link with no router required, lower latency, and more reliable connections.

A single dongle can pair up to 255 trackers, with up to 19 connected simultaneously (ESP-NOW hardware limit).

## Supported Boards

| Board | Chip | Data Mode |
|-------|------|-----------|
| Proton Dongle | ESP32-S3 | USB HID |
| Seeed XIAO ESP32-S3 | ESP32-S3 | USB HID |
| SlimeVR Dongle S3 | ESP32-S3 | USB HID |
| SlimeVR Dongle S2 | ESP32-S2 | USB HID |
| SuperMini ESP32-S3 | ESP32-S3 | USB HID |
| SlimeVR Dongle S2 (Serial) | ESP32-S2 | Serial |
| SlimeVR Dongle C2 | ESP32-C2 | Serial |
| SlimeVR Dongle C5 | ESP32-C5 | Serial |
| SlimeVR Dongle C6 | ESP32-C6 | Serial |

**USB HID** boards appear as a USB device on your PC and work directly with the SlimeVR server -- no extra software needed.

**Serial** boards communicate over UART and need a bridge to forward tracker data to the SlimeVR server (see [Serial Boards](#serial-boards) below).

> USB HID-capable boards can also be built in serial mode for development and debugging purposes. See the `_serial` build environments in `platformio.ini` (e.g., `slime_dongle_s2_serial`).

## Getting Started

### Requirements

- A supported dongle board (see table above)
- Trackers running [compatible ESP-NOW firmware](https://github.com/mitzey234/SlimeVR-Tracker-ESP/tree/esp-now) (standard SlimeVR tracker firmware will not work)
- [SlimeVR Server](https://docs.slimevr.dev/server/index.html) installed on your PC
- For serial boards: the [serial bridge](#serial-bridge) program
- The [SlimeVR Dongle Manager](#dongle-manager), selecting the right fork depending on the data mode.

### Building and Flashing

1. Install [PlatformIO](https://platformio.org/)
2. Flash your board:
   ```
   pio run -e <board_env> -t upload
   ```
   Replace `<board_env>` with your board name from `platformio.ini` (e.g., `Proton_Dongle`, `slime_dongle_8266`).

To build firmware for all boards at once:
```
python build_all.py
```

To add support for a new board, create a board JSON in `boards/`, a variant directory with `pins_arduino.h` under `variants/`, and a new `[env]` entry in `platformio.ini`.

## Dongle Manager

The [SlimeVR ESP Dongle Manager](https://github.com/ReSummit/SlimeVR-ESP-Dongle-Manager/tree/feature/serial-bridge) provides a GUI for managing the dongle -- pairing/unpairing trackers, changing WiFi channels, triggering environment scans, and monitoring tracker status without needing the physical button.

> The [original Dongle Manager](https://github.com/mitzey234/SlimeVR-ESP-Dongle-Manager) supports HID boards only. Use the fork linked above if you have a serial board.

> **Important:** The dongle manager's connection to the dongle and the serial bridge cannot run at the same time. Both use the serial port to communicate with the dongle, and only one can hold the connection. Close the bridge before connecting to the dongle via the dongle manager, and vice versa.

## Serial Bridge

Boards without USB HID (ESP32-C2/C5/C6, ESP8266) communicate over UART. The serial bridge reads framed tracker data from the dongle's serial port and forwards it to the SlimeVR server over UDP.

### Running the Bridge

```
pip install pyserial
python bridge/slimevr_serial_bridge.py
```

The bridge auto-detects the dongle's serial port and connects at 921600 baud. Tracker data is forwarded to the SlimeVR server at `127.0.0.1:6969` by default.

### Bridge Options

| Option | Description |
|--------|-------------|
| `--port <port>` | Serial port (auto-detected if omitted) |
| `--baud <rate>` | Baud rate (default: 921600) |
| `--forward <mode>` | Forwarding mode: `slimevr`, `udp`, `tcp-client`, or `none` (default: `slimevr`) |
| `--forward-host <host>` | Target host (default: `127.0.0.1`) |
| `--forward-port <port>` | Target port (default: `6969`) |
| `--print-debug` | Echo the dongle's debug text to the terminal |
| `--hexdump` | Hex dump of each forwarded transfer |
| `--stats-interval <sec>` | Interval for stats output (default: `2.0`) |

## OTA Updates

Tracker firmware can be updated over-the-air through the dongle using the tools in `nodeProgram/`. This sends an OTA command to all connected trackers, which then download the new firmware over WiFi.

---

## Original README

# SlimeVR HID ESPNow Dongle

This is a project that implements an alternative communication method for
SlimeVR trackers using the [ESPNow Protocol](https://www.espressif.com/en/solutions/low-power-solutions/esp-now) 
available on ESP devices. This dongle should allow for connecting up to 19 trackers to a single dongle at once, and allow pairing up to 255 trackers to a single dongle (although only 19 can be connected at once due to ESP-Now limitations). The dongle connects to the PC through USB and emulates a USB HID device, sending the tracker data to the SlimeVR server.

## Building and Flashing

The original firmware currently only supports ESP32-S2 & ESP32-S3 based devices. This variant expands support to othe microcontrollers, provided a stable serial communication interface exists. To get a board working, add the necessary JSON file 
in the `boards/` directory and create a new directory and `pins_arduino.h` file
under `variants/`. After that, adding a new `env` definition in the
platformio.ini file should work.

To flash the dongle, run the `pio run -t upload` command.

All board types can be built using `python .\build_all.py`

## Usage

To use the dongle, it needs to be connected to a PC through USB. You also need your trackers to have [compatible firmware](https://github.com/mitzey234/SlimeVR-Tracker-ESP/tree/esp-now) flashed onto them.

You can also control this dongle through a serial console, which is useful for debugging and for pairing trackers without the need to press the button on the dongle. I recommend using [SlimeVR ESP Dongle Manager](https://github.com/mitzey234/SlimeVR-ESP-Dongle-Manager) for this, as it provides a nice GUI for managing the dongle and its paired trackers.

The trackers will require pairing the first time you set them up. To achieve
this, first you need to put the dongle into pairing mode by pressing the button
on it once while the trackers are in pairing mode. Pairing mode is indicated by a rapidly flashing light on both the dongle and the tracker. If the pairing was successful, the tracker will stop flashing rapdidly and switch to a slower infrequent flashing pattern. The dongle only exits pairing mode after 60 seconds of inactivity, or if you press the button on it again. Pairing inactivity is reset when a new tracker is paired.

After pairing, when you turn the tracker on from that point on, it will attempt to
connect to its saved dongle.

If everything went as expected, the tracker should now appear in the SlimeVR server.

If the dongle LED flashes with longer flashes after being plugged in, it means it is scanning the wireless environment for the best channel to use. This process is necessary for optimal performance, and it only happens the first time the dongle is powered on. Once the scanning is complete, the dongle will save the best channel found and use it for future connections. The LED will stop flashing rapidly once the scanning is complete. You can also exit the scanning mode manually by pressing and holding the button on the dongle for 5 seconds (it will not unpair your trackers).

To rescan the environment, press the button on the dongle twice quickly. Note that you cannot enter pairing mode while the dongle is scanning the environment.

If you ever want to pair the tracker to a different dongle your best course of action is to put it into pairing mode with UART serial commands, or by unpairing all trackers WHILE they are connected to the dongle.

If you press and hold the pair button on the dongle for more than 5 seconds, it will unpair all connected trackers as well as erase all saved pairing information for any trackers (connected or not). It will also tell all connected trackers to unpair themselves and enter pairing mode, allowing you to pair them to a different dongle if desired.

## Errors

In case something goes wrong, theres not a lot of debugging information available apart from the serial console.

That being said don't expect me to provide very much support for this project, as I made it as a proof of concept that hopefully others can build upon to perhaps create ESP-now support for SlimeVR ESP based trackers.
