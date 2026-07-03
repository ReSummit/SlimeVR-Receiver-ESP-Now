#include "SetSecurityCommand.h"

#include <LittleFS.h>

#include "../configuration.h"
#include "hal/common_espnow.h"
#include "CommandParsing.h"

bool handleSetSecurityCommand(const String &command) {
    if (!command.startsWith("setsecurity ")) {
        return false;
    }

    String hexStr = command.substring(12);
    hexStr.trim();

    uint8_t code[8];
    if (!parseHexString(hexStr, code, 8)) {
        if (hexStr.length() != 16) {
            Serial.println("[CMD] Invalid hex string length. Use 16 hex digits.");
        } else {
            Serial.println("[CMD] Invalid hex string. Use 16 hex digits.");
        }
        return true;
    }

    auto file = LittleFS_Platform_Open("/securityCode.bin", "w", true);
    file.write(code, 8);
    file.close();

    Serial.print("[CMD] Security code set to: ");
    for (int i = 0; i < 8; i++) {
        Serial.printf("%02x", code[i]);
    }
    Serial.println();

    Configuration::getInstance().getSecurityCode(ESPNowCommunication::getInstance().securityCode);
    return true;
}