#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>
#ifdef __cplusplus
// C++ has bool built-in
#else
#include <stdbool.h>
#endif

static const uint8_t USER_BUTTON = 0;

#define USB_VID          0x1209
#define USB_PID          0x7690
#define USB_MANUFACTURER "SlimeVR"
#define USB_PRODUCT      "SlimeVR ESPNow Dongle 8266"
#define USB_SERIAL       "SVRDGA1B2C3D4E5F6"

// Default USB FirmwareMSC Settings
#define USB_FW_MSC_VENDOR_ID        "ESP8266"     // max 8 chars
#define USB_FW_MSC_PRODUCT_ID       "Firmware MSC" // max 16 chars
#define USB_FW_MSC_PRODUCT_REVISION "1.23"         // max 4 chars
#define USB_FW_MSC_VOLUME_NAME      "8266-Firm"  // max 11 chars
#define USB_FW_MSC_SERIAL_NUMBER    0x00000000

static const bool USER_BUTTON_ACTIVE_LEVEL = false;

static const uint8_t LED_BUILTIN = 2;
#define BUILTIN_LED LED_BUILTIN  // backward compatibility
#define LED_BUILTIN LED_BUILTIN  // allow testing #ifdef LED_BUILTIN
static const bool LED_ACTIVE_LEVEL = 0;

#endif /* Pins_Arduino_h */