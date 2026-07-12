#pragma once

#include <Arduino.h>  // pulls in soc/soc_caps.h (SOC_USB_OTG_SUPPORTED)

// USE_USB_HID governs whether this build presents a USB HID device (the SlimeVR
// feeder-app path) or streams over UART (serial-only path, relayed by
// slimevr_serial_bridge.py).
//
// USB HID requires the USB-OTG / TinyUSB peripheral. That capability is:
//   - present on S2/S3/P4     (SOC_USB_OTG_SUPPORTED=1, ARDUINO_USB_MODE=0)
//   - absent on classic ESP32 (no USB at all)
//   - absent on C3/C5/C6/H2   (USB-Serial-JTAG only, ARDUINO_USB_MODE=1)
//   - absent on ESP8266       (neither macro defined -> resolves to 0)
//
// SERIAL_USB_ONLY is an explicit per-variant override (set in pins_arduino.h) to
// force serial mode even on HID-capable hardware, e.g. an S2/S3 serial dongle.
#if defined(SOC_USB_OTG_SUPPORTED) && SOC_USB_OTG_SUPPORTED \
    && !ARDUINO_USB_MODE && !defined(SERIAL_USB_ONLY)
  #define USE_USB_HID 1
#else
  #define USE_USB_HID 0
#endif
