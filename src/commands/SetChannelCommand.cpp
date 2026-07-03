#include "SetChannelCommand.h"

#include "../configuration.h"
#include "hal/common_espnow.h"

bool handleSetChannelCommand(const String &command) {
    if (!command.startsWith("setchannel ")) {
        return false;
    }

    bool scanning = ESPNowCommunication::getInstance().isScanningEnvironment();
    if (scanning) {
        // Don't allow changing channel while scanning environment, as it can cause issues with the scanning process
        Serial.println("[CMD] Cannot change WiFi channel while environment scanning is active. Please stop scanning first.");
        return true; // Return true since we recognized the command, even though we didn't execute it
    }

    String chStr = command.substring(10);
    chStr.trim();
    int ch = chStr.toInt();

    if (ch >= 1 && ch <= 14) {
        Configuration::getInstance().setWifiChannel((uint8_t)ch);
        Serial.printf("[CMD] WiFi channel set to %d and saved.\n", ch);
    } else {
        Serial.println("[CMD] Invalid channel. Use 1-14.");
    }

    return true;
}