#include "Serial.h"

#if defined(ARDUINO_ARCH_ESP32)

#if !(defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT)
USBCDC USBSerial;
#endif
HybridSerial Serial;

#elif defined(ARDUINO_ARCH_RP2040)

#undef Serial
HybridSerial SlimeSerial(&Serial);

#endif
