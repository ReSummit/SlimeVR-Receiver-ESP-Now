#include "UnpairAllCommand.h"

#include "hal/common_espnow.h"

bool handleUnpairAllCommand(const String &command) {
    if (!command.equalsIgnoreCase("unpairall")) {
        return false;
    }

    Serial.println("[CMD] Unpairing and disconnecting all trackers...");
    ESPNowCommunication::getInstance().UnpairAllTrackers();
    return true;
}