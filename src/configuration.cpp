#include "configuration.h"
#include <Arduino.h>
#include <algorithm>
#include "hal/common.h"
#include "hal/common_espnow.h"
#include "./serialCom/SerialCom.h"

#define DEFAULT_WIFI_CHANNEL 6

#define STARTING_TRACKER_ID 0

Configuration &Configuration::getInstance() {
    return instance;
}

// Iterate all paired trackers, calling cb(mac, trackerId)
void Configuration::forEachPairedTracker(std::function<void(const uint8_t mac[6], uint8_t trackerId)> cb) {
    if (!LittleFS.exists(pairedTrackersPath) || !LittleFS.exists(trackerIdsPath)) return;
    auto macFile = LittleFS.open(pairedTrackersPath, "r");
    auto idFile = LittleFS.open(trackerIdsPath, "r");
    while (macFile.available()) {
        uint8_t mac[6];
        if (macFile.read(mac, 6) == 6) {
            // Find trackerId for this MAC
            idFile.seek(0);
            uint8_t idMac[6];
            uint8_t trackerId = 255;
            while (idFile.available()) {
                if (idFile.read(idMac, 6) == 6 && idFile.read(&trackerId, 1) == 1) {
                    if (memcmp(mac, idMac, 6) == 0) {
                        break;
                    }
                }
            }
            cb(mac, trackerId);
        }
    }
    macFile.close();
    idFile.close();
}

void Configuration::setWifiChannel(uint8_t channel) {
    auto result = WiFi_Platform_SetChannel(channel);
    if (result != 0) {
        Serial.printf("[Config] Failed to set WiFi channel to %d - error %d\n", channel, result);
        return;
    }
    auto file = LittleFS_Platform_Open(wifiChannelPath, "w", true);
    file.write(&channel, 1);
    file.close();
    ESPNowCommunication::channel = channel;
    Serial.printf("[Config] WiFi channel set to %d and saved to %s\n", channel, wifiChannelPath);
    ESPNowCommunication::getInstance().disconnectAllTrackers();
    if (SlimeVR::SerialCom::comEnabled()) {
        SlimeVR::SerialComMessages::UpdateChannel::print();
    }
}

uint8_t Configuration::getWifiChannel() {
    if (!LittleFS.exists(wifiChannelPath)) {
        return DEFAULT_WIFI_CHANNEL; // Default channel
    }
    auto file = LittleFS.open(wifiChannelPath, "r");
    uint8_t channel = DEFAULT_WIFI_CHANNEL;
    file.read(&channel, 1);
    file.close();
    return channel;
}

bool Configuration::wifiChannelFileExists() {
    return LittleFS.exists(Configuration::wifiChannelPath);
}

size_t Configuration::getSavedTrackerCount() {
    if (!LittleFS.exists(trackerIdsPath)) return 0;

    size_t count = 0;
    auto file = LittleFS.open(trackerIdsPath, "r");
    uint8_t mac[6];
    uint8_t trackerId;
    while (file.available()) {
        if (file.read(mac, 6) == 6 && file.read(&trackerId, 1) == 1) {
            count++;
        }
    }
    file.close();
    return count;
}

bool Configuration::isPairedTrackerCapacityReached() {
    return getSavedTrackerCount() >= maxPairedTrackers;
}

// Get all paired tracker MACs
std::vector<std::array<uint8_t, 6>> Configuration::getAllPairedTrackerMacs() {
    std::vector<std::array<uint8_t, 6>> macs;
    if (!LittleFS.exists(pairedTrackersPath)) return macs;
    auto file = LittleFS.open(pairedTrackersPath, "r");
    while (file.available()) {
        std::array<uint8_t, 6> mac;
        if (file.read(mac.data(), 6) == 6) {
            macs.push_back(mac);
        }
    }
    file.close();
    return macs;
}

// Get all paired tracker IDs
std::vector<uint8_t> Configuration::getAllPairedTrackerIds() {
    std::vector<uint8_t> ids;
    if (!LittleFS.exists(trackerIdsPath)) return ids;
    auto file = LittleFS.open(trackerIdsPath, "r");
    uint8_t mac[6];
    uint8_t trackerId;
    while (file.available()) {
        if (file.read(mac, 6) == 6 && file.read(&trackerId, 1) == 1) {
            ids.push_back(trackerId);
        }
    }
    file.close();
    return ids;
}

void Configuration::setup() {
    bool status = LittleFS.begin();
    if (!status) {
        Serial.println("Could not mount LittleFS, formatting");

        status = LittleFS.format();
        if (!status) {
            Serial.println("Could not format LittleFS, aborting");
            return;
        }

        status = LittleFS.begin();
        if (!status) {
            Serial.println("Could not mount LittleFS, aborting");
            return;
        }
    }
    Serial.println("LittleFS is mounted");
}

bool Configuration::isTrackerIdInUse(uint8_t trackerId) {
    if (!LittleFS.exists(trackerIdsPath)) return false;
    auto file = LittleFS.open(trackerIdsPath, "r");
    uint8_t mac[6];
    uint8_t id;
    while (file.available()) {
        if (file.read(mac, 6) == 6 && file.read(&id, 1) == 1) {
            if (id == trackerId) {
                file.close();
                return true;
            }
        }
    }
    file.close();
    return false;
}

void Configuration::getSecurityCode(uint8_t securityCode[8]) {
    if (!LittleFS.exists(securityCodePath)) {
        Serial.println("Security code doesn't exist, generating new one");
        
        // Generate random 8-byte security code
        for (int i = 0; i < 8; i++) {
            securityCode[i] = random(0, 256);
        }
        
        // Save to file
        auto file = LittleFS_Platform_Open(securityCodePath, "w", true);
        file.write(securityCode, 8);
        file.close();
        
        Serial.printf("Generated security code: %02x%02x%02x%02x%02x%02x%02x%02x\n",
                     securityCode[0], securityCode[1], securityCode[2], securityCode[3],
                     securityCode[4], securityCode[5], securityCode[6], securityCode[7]);
    } else {
        // Load existing security code
        auto file = LittleFS.open(securityCodePath, "r");
        file.read(securityCode, 8);
        file.close();
        
        Serial.printf("Loaded security code: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
                     securityCode[0], securityCode[1], securityCode[2], securityCode[3],
                     securityCode[4], securityCode[5], securityCode[6], securityCode[7]);
    }
}

void Configuration::resetSecurityCode() {
    if (LittleFS.exists(securityCodePath)) {
        LittleFS.remove(securityCodePath);
        Serial.println("Security code reset");
        uint8_t dummy[8];
        Configuration::getInstance().getSecurityCode(ESPNowCommunication::getInstance().securityCode); // Reload into ESPNowCommunication
    }
}

bool Configuration::isPairedTracker(const uint8_t mac[6]) {
    if (!LittleFS.exists(pairedTrackersPath)) {
        return false;
    }
    
    auto file = LittleFS.open(pairedTrackersPath, "r");
    uint8_t storedMac[6];
    
    while (file.read(storedMac, 6) == 6) {
        if (memcmp(storedMac, mac, 6) == 0) {
            file.close();
            return true;
        }
    }
    
    file.close();
    return false;
}

void Configuration::addPairedTracker(const uint8_t mac[6]) {
    if (isPairedTracker(mac)) {
        return; // Already paired
    }
    
    auto file = LittleFS.open(pairedTrackersPath, "a");
    file.write(mac, 6);
    file.close();

    if (SlimeVR::SerialCom::comEnabled()) {
        uint8_t trackerId;
        if (getTrackerIdForMac(mac, trackerId)) {
            uint8_t macCopy[6];
            memcpy(macCopy, mac, 6);
            SlimeVR::SerialComMessages::TrackerPaired::print(macCopy, trackerId); // Notify pairing
        }
    }
}

void Configuration::removePairedTracker(const uint8_t mac[6]) {
    if (!LittleFS.exists(pairedTrackersPath)) return;
    
    // Read all MAC addresses except the one to remove
    std::vector<uint8_t> remainingMacs;
    auto file = LittleFS.open(pairedTrackersPath, "r");
    uint8_t storedMac[6];
    
    while (file.read(storedMac, 6) == 6) {
        if (memcmp(storedMac, mac, 6) != 0) {
            for (int i = 0; i < 6; i++) {
                remainingMacs.push_back(storedMac[i]);
            }
        }
    }
    file.close();
    
    // Write back the remaining MAC addresses
    file = LittleFS.open(pairedTrackersPath, "w");
    file.write(remainingMacs.data(), remainingMacs.size());
    file.close();

    uint8_t trackerId;
    // Remove tracker ID for this MAC
    if (LittleFS.exists(trackerIdsPath)) {
        std::vector<uint8_t> remainingData;
        auto idFile = LittleFS.open(trackerIdsPath, "r");
        uint8_t idMac[6];
        while (idFile.read(idMac, 6) == 6 && idFile.read(&trackerId, 1) == 1) {
            if (memcmp(idMac, mac, 6) != 0) {
                for (int i = 0; i < 6; i++) {
                    remainingData.push_back(idMac[i]);
                }
                remainingData.push_back(trackerId);
            }
        }
        idFile.close();

        // Write back all except the removed one
        idFile = LittleFS.open(trackerIdsPath, "w");
        if (!remainingData.empty()) {
            idFile.write(remainingData.data(), remainingData.size());
        }
        idFile.close();
    }
    
    Serial.printf("Removed paired tracker: %02x:%02x:%02x:%02x:%02x:%02x\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    if (SlimeVR::SerialCom::comEnabled()) {
        uint8_t macCopy[6];
        memcpy(macCopy, mac, 6);
        SlimeVR::SerialComMessages::TrackerUnpaired::print(macCopy, trackerId); // Notify unpairing
    }
}

void Configuration::clearAllPairedTrackers() {
    if (LittleFS.exists(pairedTrackersPath)) {
        LittleFS.remove(pairedTrackersPath);
        Serial.println("Cleared all paired trackers");
    }
    if (LittleFS.exists(trackerIdsPath)) {
        LittleFS.remove(trackerIdsPath);
        Serial.println("Cleared all tracker IDs");
    }
}

bool Configuration::getTrackerIdForMac(const uint8_t mac[6], uint8_t &trackerId) {
    if (!LittleFS.exists(trackerIdsPath)) {
        // No tracker IDs file exists, allocate new ID
        return allocateTrackerIdForMac(mac, trackerId);
    }
    
    // Search for existing tracker ID
    auto file = LittleFS.open(trackerIdsPath, "r");
    uint8_t storedMac[6];
    uint8_t storedTrackerId;
    
    while (file.available()) {
        if (file.read(storedMac, 6) == 6 && file.read(&storedTrackerId, 1) == 1) {
            if (memcmp(storedMac, mac, 6) == 0) {
                file.close();
                Serial.printf("Found existing tracker ID %d for MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
                             storedTrackerId, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
                trackerId = storedTrackerId;
                return true;
            }
        }
    }
    file.close();
    
    // MAC not found, allocate new ID
    return allocateTrackerIdForMac(mac, trackerId);
}

bool Configuration::allocateTrackerIdForMac(const uint8_t mac[6], uint8_t &trackerId) {
    // Read all existing tracker IDs to find first available
    bool usedIds[256] = {false};
    
    if (LittleFS.exists(trackerIdsPath)) {
        auto file = LittleFS.open(trackerIdsPath, "r");
        uint8_t storedMac[6];
        uint8_t storedTrackerId;
        
        while (file.available()) {
            if (file.read(storedMac, 6) == 6 && file.read(&storedTrackerId, 1) == 1) {
                usedIds[storedTrackerId] = true;
            }
        }
        file.close();
    }
    
    // Find first available ID (starting from STARTING_TRACKER_ID)
    bool found = false;
    uint8_t newId = 0;
    for (uint16_t candidate = STARTING_TRACKER_ID; candidate < 256; candidate++) {
        if (!usedIds[candidate]) {
            newId = static_cast<uint8_t>(candidate);
            found = true;
            break;
        }
    }

    if (!found) {
        Serial.println("[Config] Cannot allocate tracker ID: capacity reached (256 trackers). Unpair trackers first.");
        return false;
    }
    
    // Store the new MAC -> ID mapping
    auto file = LittleFS.open(trackerIdsPath, "a");
    file.write(mac, 6);
    file.write(&newId, 1);
    file.close();

    trackerId = newId;
    
    Serial.printf("Allocated new tracker ID %d for MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
                 newId, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    return true;
}

Configuration Configuration::instance;
