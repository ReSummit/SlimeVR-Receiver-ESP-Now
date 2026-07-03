#pragma once

#include "MessageBuilder.h"
#include "./SerialCom.h"
#include "../GlobalVars.h"
#include "hal/common_espnow.h"

namespace SlimeVR
{
    namespace SerialComMessages
    {
        class EnvironmentScanResults {
            public:
                static bool print(uint32_t channelBytesSeen[12], uint8_t selected) {
                    // Constructor can be used for initialization if needed
                    MessageBuilder builder;
                    builder.writeStringRaw(SlimeVR::SerialCom::getPrefix());
                    builder.writeUInt8(static_cast<uint8_t>(SerialComMessageTypes::ENVIRONMENT_SCAN_RESULT));
                    builder.writeUInt8(selected);
                    int length = 12;
                    builder.writeUInt8(length-1); // We don't send channel 0, so length is 11, but we write it as 12 to indicate that channels are 1-indexed
                    for (int i = 1; i < length; i++) builder.writeUInt32(channelBytesSeen[i]);
                    builder.print();
                    builder.Destroy(); // Clean up buffer after use
                    return true;
                }
        };
    }
}