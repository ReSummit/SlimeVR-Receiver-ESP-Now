#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>
#ifdef __cplusplus
// C++ has bool built-in
#else
#include <stdbool.h>
#endif

// Default USB Settings
#define USB_VID          0x1209
#define USB_PID          0x7690
#define USB_MANUFACTURER "SlimeVR"
#define USB_PRODUCT      "SlimeVR ESPNow Dongle"
#define USB_SERIAL       "SVRDGA1B2C3D4E5F6"

// Default USB FirmwareMSC Settings
#define USB_FW_MSC_VENDOR_ID        "ESP32-C6"     // max 8 chars
#define USB_FW_MSC_PRODUCT_ID       "Firmware MSC" // max 16 chars
#define USB_FW_MSC_PRODUCT_REVISION "1.23"         // max 4 chars
#define USB_FW_MSC_VOLUME_NAME      "C6-Firmware"  // max 11 chars
#define USB_FW_MSC_SERIAL_NUMBER    0x00000000

// Defines for Serial / HID Selection
#define SERIAL_USB_ONLY
#define SERIAL_BAUD_RATE 921600

// TODO: verify these pins for your C6 hardware
// C6 devkit onboard RGB LED is on GPIO8, boot button on GPIO9
static const uint8_t LED_BUILTIN = 8;
#define BUILTIN_LED LED_BUILTIN // backward compatibility
#define LED_BUILTIN LED_BUILTIN // allow testing #ifdef LED_BUILTIN
static const bool LED_ACTIVE_LEVEL = true;

static const uint8_t USER_BUTTON = 9;
static const bool USER_BUTTON_ACTIVE_LEVEL = false;

// Boot mode pin - when pulled low, prevents USB/HID initialization
static const uint8_t BOOT_MODE_PIN = 5;
static const bool BOOT_MODE_ACTIVE_LEVEL = false; // Active LOW (pulled down)

#endif /* Pins_Arduino_h */
