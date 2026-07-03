#pragma once

#include <Arduino.h>
#include "MessageBuilder.h"
#include "./SerialCom.h"
#include "../GlobalVars.h"
#include "../configuration.h"

namespace SlimeVR
{
    namespace SerialComMessages
    {
        class Ident {
            public:
                static bool print() {
                    // Constructor can be used for initialization if needed
                    MessageBuilder builder;
                    builder.writeStringRaw(SlimeVR::SerialCom::getPrefix());
                    builder.writeUInt8(static_cast<uint8_t>(SerialComMessageTypes::IDENT));
                    builder.writeString(USB_PRODUCT);
                    builder.writeString(FIRMWARE_VERSION);
                    builder.writeString(ARDUINO_VARIANT);
                    builder.writeUInt32(millis() / 1000UL); // Uptime in seconds
                    uint8_t macaddr[6];
                    WiFi.macAddress(macaddr);
                    builder.writeMacAddress(macaddr);
                    builder.writeUInt8(WiFi.channel());
                    builder.writeBool(espnow.isInPairingMode());
                    builder.writeBool(espnow.isScanningEnvironment());
                    builder.writeUInt8(espnow.getConnectedTrackerCount());
                    for (size_t i = 0; i < espnow.getConnectedTrackerCount(); i++) {
                        builder.writeTracker(*espnow.getTrackerByIndex(i)); // Write tracker info to message
                    }
                    uint8_t pairedCount = 0;
                    Configuration::getInstance().forEachPairedTracker([&pairedCount](const uint8_t mac[6], uint8_t trackerId) {
                        pairedCount++;
                    });
                    builder.writeUInt8(pairedCount);
                    Configuration::getInstance().forEachPairedTracker([&builder](const uint8_t mac[6], uint8_t trackerId) {
                        builder.writePairedTracker(mac, trackerId); // Write paired tracker info to message
                    });
                    builder.print();
                    builder.Destroy(); // Clean up buffer after use
                    return true;
                }
        };
    }
}