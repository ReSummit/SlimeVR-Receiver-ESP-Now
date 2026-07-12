#include "Serial.h"

#if !(defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT) && USE_USB_HID
USBCDC USBSerial;
HybridSerial Serial;
#elif defined(ARDUINO_ARCH_ESP8266)
#undef Serial
HybridSerial SlimeSerial(&Serial);
#else
HybridSerial Serial;
#endif
