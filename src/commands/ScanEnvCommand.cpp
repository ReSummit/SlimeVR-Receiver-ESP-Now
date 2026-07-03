#include "ScanEnvCommand.h"

#include "hal/common_espnow.h"

bool handleScanEnvCommand(const String &command) {
    if (!command.equalsIgnoreCase("scanenv")) {
        return false;
    }

    bool scanning = ESPNowCommunication::getInstance().isScanningEnvironment();
    if (!scanning) {
        ESPNowCommunication::getInstance().enterEnvironmentScanningMode();
        Serial.println("[CMD] Environment scanning mode enabled.");
    } else {
        ESPNowCommunication::getInstance().exitEnvironmentScanningMode();
        Serial.println("[CMD] Environment scanning mode disabled.");
    }

    return true;
}