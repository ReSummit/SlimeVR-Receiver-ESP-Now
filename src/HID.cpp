#include "usb_mode.h"

#if USE_USB_HID

#include "HID.h"
#include "GlobalVars.h"
#include "serialCom/SerialCom.h"

#include <cstring>
#include <Arduino.h>

#include <WiFi.h>
#include "USB.h"

static void usbEventCallback(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_base == ARDUINO_USB_EVENTS) {
    arduino_usb_event_data_t *data = (arduino_usb_event_data_t *)event_data;
    switch (event_id) {
      case ARDUINO_USB_STARTED_EVENT: break;  // USB PLUGGED
      case ARDUINO_USB_STOPPED_EVENT: break;  // USB UNPLUGGED
      case ARDUINO_USB_SUSPEND_EVENT: break;  // USB SUSPENDED
      case ARDUINO_USB_RESUME_EVENT:  break;  // USB RESUMED

      default: break;
    }
  } else if (event_base == ARDUINO_USB_CDC_EVENTS) {
    arduino_usb_cdc_event_data_t *data = (arduino_usb_cdc_event_data_t *)event_data;
    switch (event_id) {
      case ARDUINO_USB_CDC_CONNECTED_EVENT:    break;  // CDC CONNECTED
      case ARDUINO_USB_CDC_DISCONNECTED_EVENT:
        // CDC DISCONNECTED
        SlimeVR::SerialCom::getInstance().enabled = false;
        break;
      case ARDUINO_USB_CDC_LINE_STATE_EVENT:   break;  // CDC LINE STATE
      case ARDUINO_USB_CDC_LINE_CODING_EVENT:  break;  // CDC LINE CODING
      case ARDUINO_USB_CDC_RX_EVENT:           break;  // CDC RX
      case ARDUINO_USB_CDC_RX_OVERFLOW_EVENT:  break;  // CDC RX Overflow

      default: break;
    }
  }
}


HIDDevice::HIDDevice() {
    if (initialized) {
        return;
    }

    initialized = true;
    HID.addDevice(this, sizeof(hid_report_desc));
}

void HIDDevice::begin() {
    uint8_t mac[6];
    if (WiFi.getMode() == WIFI_MODE_NULL) {
        WiFi.mode(WIFI_STA);
        delay(100);
    }
    WiFi.macAddress(mac);

    // Format for USB_SERIAL: SVRDG + last 6 hex digits (e.g., SVRDGA1B2C3D4E5F6)
    char usbSerial[20] = "SVRDG";
    // Append full MAC address (12 hex digits) to serial string
    snprintf(usbSerial + 5, sizeof(usbSerial) - 5, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    USB.serialNumber(usbSerial);
    USB.productName(USB_PRODUCT);
    USB.manufacturerName(USB_MANUFACTURER);
    USB.VID(USB_VID);
    USB.PID(USB_PID);
    USB.begin();
    Serial.begin(115200);
    delay(10);
    Serial.printf("USB Serial Number: %s\n", usbSerial);
    HID.begin();
    
    USB.onEvent(usbEventCallback);
    USBSerial.onEvent(usbEventCallback);
}

uint16_t HIDDevice::_onGetDescriptor(uint8_t *buffer) {
    memcpy(buffer, hid_report_desc, sizeof(hid_report_desc));
    return sizeof(hid_report_desc);
}

bool HIDDevice::send(const uint8_t *value, size_t size) {
    // Only attempt send if HID is actually ready to prevent error spam
    if (!HID.ready()) {
        return false;
    }
    // Use shorter timeout to prevent blocking when USB is congested
    return HID.SendReport(0, value, size, 10);
}

bool HIDDevice::ready() {
    return HID.ready();
}

bool HIDDevice::initialized = false;

#endif