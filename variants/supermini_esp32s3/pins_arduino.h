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
#define USB_FW_MSC_VENDOR_ID        "ESP32-S3"     // max 8 chars
#define USB_FW_MSC_PRODUCT_ID       "Firmware MSC" // max 16 chars
#define USB_FW_MSC_PRODUCT_REVISION "1.23"         // max 4 chars
#define USB_FW_MSC_VOLUME_NAME      "S3-Firmware"  // max 11 chars
#define USB_FW_MSC_SERIAL_NUMBER    0x00000000

static const uint8_t LED_BUILTIN = 48;
#define BUILTIN_LED LED_BUILTIN // backward compatibility
#define LED_BUILTIN LED_BUILTIN // allow testing #ifdef LED_BUILTIN
static const bool LED_ACTIVE_LEVEL = 0;

static const uint8_t USER_BUTTON = 0;
static const bool USER_BUTTON_ACTIVE_LEVEL = false;

static const uint8_t BOOT_MODE_PIN = 0;
static const bool BOOT_MODE_ACTIVE_LEVEL = false;

#endif /* Pins_Arduino_h */

