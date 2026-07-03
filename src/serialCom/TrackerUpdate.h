#pragma once

#include "MessageBuilder.h"
#include "./SerialCom.h"
#include "../GlobalVars.h"
#include "hal/common_espnow.h"

namespace SlimeVR
{
    namespace SerialComMessages
    {
        class TrackerUpdate {
            public:
                static bool print(uint16_t bytesPerSecond, uint16_t packetsPerSecond) {
                    // Constructor can be used for initialization if needed
                    MessageBuilder builder;
                    builder.writeStringRaw(SlimeVR::SerialCom::getPrefix());
                    builder.writeUInt8(static_cast<uint8_t>(SerialComMessageTypes::TRACKER_UPDATE));
                    builder.writeUInt16(bytesPerSecond);
                    builder.writeUInt16(packetsPerSecond);
                    builder.writeInt8(static_cast<int8_t>(std::round(platformTemperature())));
                    builder.writeUInt8(espnow.getConnectedTrackerCount());
                    for (size_t i = 0; i < espnow.getConnectedTrackerCount(); i++) {
                        builder.writeTracker(*espnow.getTrackerByIndex(i)); // Write tracker info to message
                    }
                    builder.print();
                    builder.Destroy(); // Clean up buffer after use
                    return true;
                }
        };
    }
}