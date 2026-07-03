#include <cstdint>
#include <string>
#include "hal/common_espnow.h"

#pragma once

namespace SlimeVR
{
    class MessageBuilder
    {
        private:
            uint8_t* buffer;
            uint16_t bufferSize = 0;
            uint16_t head = 0;
        public:
            MessageBuilder();
            ~MessageBuilder();
            bool writeStringRaw(const std::string& str);
            bool writeString(const std::string& str);
            bool writeBytes(const uint8_t* data, uint16_t length);
            bool writeUInt8(uint8_t value);
            bool writeUInt16(uint16_t value);
            bool writeUInt32(uint32_t value);
            bool writeMacAddress(const uint8_t mac[6]);
            bool writeTracker(ESPNowCommunication::Tracker tracker);
            bool writePairedTracker(const uint8_t mac[6], uint8_t trackerId);
            bool writeInt8(int8_t value);
            bool writeInt16(int16_t value);
            bool writeInt32(int32_t value);
            bool writeBool(bool value);
            const uint8_t* getBuffer() const { return buffer; }
            void print();
            uint16_t getSize() const { return head; }
            void Destroy() { delete[] buffer; buffer = nullptr; head = 0; bufferSize = 0; }
    };
}