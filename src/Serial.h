#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#undef Serial  // Remove the core's Serial definition


#ifndef SERIAL_H
#define SERIAL_H

#if defined(ARDUINO_USB_MODE) && !defined(SERIAL_USB_ONLY)
extern USBCDC USBSerial;
#endif

class HybridSerial : public Stream {
private:
    #if defined(ARDUINO_USB_MODE) && !defined(SERIAL_USB_ONLY)
    USBCDC* usb;
    #endif
    HardwareSerial* uart;
    SemaphoreHandle_t writeMutex;
    StaticSemaphore_t mutexBuffer;
    
    // Internal unlocked write helpers
    size_t writeUnlocked(uint8_t c) {
        size_t n = 0;
        n += uart->write(c);
        #if defined(ARDUINO_USB_MODE) && !defined(SERIAL_USB_ONLY)
        if (usb && usb->availableForWrite() > 0) {
            n += usb->write(c);
        }
        #endif
        return n;
    }
    
    size_t writeUnlocked(const uint8_t *buffer, size_t size) {
        size_t n = 0;
        n += uart->write(buffer, size);
        #if defined(ARDUINO_USB_MODE) && !defined(SERIAL_USB_ONLY)
        if (usb && usb->availableForWrite() > 0) {
            n += usb->write(buffer, size);
        }
        #endif
        return n;
    }
    
public:
    #if defined(ARDUINO_USB_MODE) && !defined(SERIAL_USB_ONLY)
    HybridSerial() : uart(&Serial0), usb(&USBSerial) {
        writeMutex = xSemaphoreCreateMutexStatic(&mutexBuffer);
    }
    
    void begin(unsigned long baud = 115200) {
        uart->begin(baud);
        usb->begin();
    }
    
    void beginUSB() {
        usb->begin();
    }
    #else
    HybridSerial() : uart(&Serial0){
        writeMutex = xSemaphoreCreateMutexStatic(&mutexBuffer);
    }
    
    void begin(unsigned long baud = 921600) {
        uart->begin(baud);
    }
    
    void beginUSB() {
        return;
    }
    #endif
    
    size_t write(uint8_t c) override {
        if (writeMutex && xSemaphoreTake(writeMutex, portMAX_DELAY) == pdTRUE) {
            size_t n = writeUnlocked(c);
            xSemaphoreGive(writeMutex);
            return n;
        }
        return 0;
    }

    size_t write(const uint8_t *buffer, size_t size) override {
        if (writeMutex && xSemaphoreTake(writeMutex, portMAX_DELAY) == pdTRUE) {
            size_t n = writeUnlocked(buffer, size);
            xSemaphoreGive(writeMutex);
            return n;
        }
        return 0;
    }
    
    size_t printf(const char *format, ...) {
        if (!writeMutex || xSemaphoreTake(writeMutex, portMAX_DELAY) != pdTRUE) {
            return 0;
        }
        
        va_list args;
        va_start(args, format);
        char buffer[256];
        int len = vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        
        size_t n = 0;
        if (len > 0) {
            n = writeUnlocked((const uint8_t*)buffer, len);
        }
        
        xSemaphoreGive(writeMutex);
        return n;
    }

    size_t writeLine(const uint8_t *buffer, size_t size) {
        if (writeMutex && xSemaphoreTake(writeMutex, portMAX_DELAY) == pdTRUE) {
            size_t n = writeUnlocked(buffer, size);
            n += writeUnlocked((const uint8_t*)"\r\n", 2);
            uart->flush();
            #if defined(ARDUINO_USB_MODE) && !defined(SERIAL_USB_ONLY)
            if (usb) usb->flush();
            #endif
            xSemaphoreGive(writeMutex);
            return n;
        }
        return 0;
    }

    size_t writeLine(const char* s) {
        return writeLine((const uint8_t*)s, strlen(s));
    }
    
    size_t println() {
        return writeLine((const uint8_t*)"", 0);
    }
    
    size_t println(const char* s) {
        return writeLine(s);
    }
    
    size_t println(const String& s) {
        return println(s.c_str());
    }
    
    // Read from both (USB has priority, then UART)
    int available() override {
        #if defined(ARDUINO_USB_MODE) && !defined(SERIAL_USB_ONLY)
        int n = usb->available();
        if (n > 0) return n;
        #endif

        return uart->available();
    }
    
    int read() override {
        #if defined(ARDUINO_USB_MODE) && !defined(SERIAL_USB_ONLY)
        if (usb->available()) {
            return usb->read();
        }
        #endif

        return uart->read();
    }
    
    int peek() override {
        #if defined(ARDUINO_USB_MODE) && !defined(SERIAL_USB_ONLY)
        if (usb->available()) {
            return usb->peek();
        }
        #endif
        
        return uart->peek();
    }
    
    void flush() override {
        if (writeMutex && xSemaphoreTake(writeMutex, portMAX_DELAY) == pdTRUE) {
            uart->flush();

            #if defined(ARDUINO_USB_MODE) && !defined(SERIAL_USB_ONLY)
            if (usb) usb->flush();
            #endif

            xSemaphoreGive(writeMutex);
        }
    }
    
    // Expose operator bool for connection checking
    #if defined(ARDUINO_USB_MODE) && !defined(SERIAL_USB_ONLY)
    operator bool() const {
        return *usb || *uart;
    }
    #else
    operator bool() const {
        return *uart;
    }
    #endif
};

#endif

extern HybridSerial Serial;
