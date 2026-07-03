#include "UnpairCommand.h"

#include "../configuration.h"
#include "hal/common_espnow.h"
#include "CommandParsing.h"

bool handleUnpairCommand(const String &command) {
    if (!command.startsWith("unpair ")) {
        return false;
    }

    String macStr = command.substring(7);
    macStr.trim();

    uint8_t mac[6];
    if (!parseMacAddress(macStr, mac)) {
        Serial.println("[CMD] Invalid MAC address format. Use XX:XX:XX:XX:XX:XX");
        return true;
    }

    Configuration::getInstance().removePairedTracker(mac);

    if (ESPNowCommunication::getInstance().isTrackerConnected(mac)) {
        ESPNowCommunication::getInstance().sendUnpairToTracker(mac);
        ESPNowCommunication::getInstance().disconnectSingleTracker(mac);
        Serial.printf("[CMD] Tracker %02x:%02x:%02x:%02x:%02x:%02x disconnected and unpaired.\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    } else {
        Serial.printf("[CMD] Tracker %02x:%02x:%02x:%02x:%02x:%02x unpaired.\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    return true;
}