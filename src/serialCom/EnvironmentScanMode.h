#pragma once

#include "MessageBuilder.h"
#include "./SerialCom.h"
#include "../GlobalVars.h"
#include "hal/common_espnow.h"

namespace SlimeVR
{
    namespace SerialComMessages
    {
        class EnvironmentScanMode {
            public:
                static bool print(int scanningTime) {
                    // Constructor can be used for initialization if needed
                    MessageBuilder builder;
                    builder.writeStringRaw(SlimeVR::SerialCom::getPrefix());
                    builder.writeUInt8(static_cast<uint8_t>(SerialComMessageTypes::ENVIRONMENT_SCAN_MODE));
                    builder.writeBool(espnow.isScanningEnvironment());
                    builder.writeUInt16(scanningTime);
                    builder.print();
                    builder.Destroy(); // Clean up buffer after use
                    return true;
                }
        };
    }
}