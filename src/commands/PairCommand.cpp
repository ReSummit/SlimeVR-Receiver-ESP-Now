#include "PairCommand.h"

#include "hal/common_espnow.h"

bool handlePairCommand(const String &command) {
    if (!command.equalsIgnoreCase("pair")) {
        return false;
    }

    bool pairing = !ESPNowCommunication::getInstance().isInPairingMode();
    if (pairing) {
        if (ESPNowCommunication::getInstance().enterPairingMode()) {
            Serial.println("[CMD] Pairing mode enabled.");
        }
    } else {
        ESPNowCommunication::getInstance().exitPairingMode();
        Serial.println("[CMD] Pairing mode disabled.");
    }
    return true;
}