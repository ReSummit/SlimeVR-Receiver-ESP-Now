#include "Serial.h"

#if !(defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_MODE && !defined(SERIAL_USB_ONLY)
USBCDC USBSerial;
HybridSerial Serial;
#else
HybridSerial Serial;
#endif

