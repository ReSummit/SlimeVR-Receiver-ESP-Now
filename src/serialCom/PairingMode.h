#pragma once

#include "MessageBuilder.h"
#include "./SerialCom.h"
#include "../GlobalVars.h"
#include "hal/common_espnow.h"

namespace SlimeVR
{
    namespace SerialComMessages
    {
        class PairingMode {
            public:
                static bool print() {
                    // Constructor can be used for initialization if needed
                    MessageBuilder builder;
                    builder.writeStringRaw(SlimeVR::SerialCom::getPrefix());
                    builder.writeUInt8(static_cast<uint8_t>(SerialComMessageTypes::PAIRING_MODE));
                    builder.writeBool(espnow.isInPairingMode());
                    builder.print();
                    builder.Destroy(); // Clean up buffer after use
                    return true;
                }
        };
    }
}