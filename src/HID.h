#pragma once

#include "usb_mode.h"

#if USE_USB_HID

#include <USBHID.h>
#include <cstddef>
#include <cstdint>
#include "Serial.h"

// HID Constants
#define HID_USAGE_GEN_DESKTOP 0x01
#define HID_USAGE_GEN_DESKTOP_UNDEFINED 0x00
#define HID_END_COLLECTION 0xC0

// clang-format off
static const uint8_t hid_report_desc[] = {
	HID_USAGE_PAGE(HID_USAGE_GEN_DESKTOP),
	HID_USAGE(HID_USAGE_GEN_DESKTOP_UNDEFINED),
	HID_COLLECTION(HID_COLLECTION_APPLICATION),
		HID_USAGE(HID_USAGE_GEN_DESKTOP_UNDEFINED),
		HID_REPORT_SIZE(8),
		HID_REPORT_COUNT(64),
		HID_INPUT(0x02),
	HID_END_COLLECTION,
};
// clang-format on

class HIDDevice : public USBHIDDevice {
public:
    HIDDevice();
    void begin();
    uint16_t _onGetDescriptor(uint8_t *buffer);
    bool send(const uint8_t *value, size_t size);
    bool ready();

private:
    static bool initialized;
    USBHID HID;
};

#endif