#pragma once

#include "MessageBuilder.h"
#include "./SerialCom.h"
#include "../GlobalVars.h"
#include "hal/common_espnow.h"

namespace SlimeVR
{
    namespace SerialComMessages
    {
        class UpdateChannel {
            public:
                static bool print() {
                    // Constructor can be used for initialization if needed
                    MessageBuilder builder;
                    builder.writeStringRaw(SlimeVR::SerialCom::getPrefix());
                    builder.writeUInt8(static_cast<uint8_t>(SerialComMessageTypes::UPDATE_CHANNEL));
                    builder.writeUInt8(WiFi.channel());
                    builder.print();
                    builder.Destroy(); // Clean up buffer after use
                    return true;
                }
        };
    }
}