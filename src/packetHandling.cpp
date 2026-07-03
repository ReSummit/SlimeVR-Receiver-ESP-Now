#include "packetHandling.h"
#include "hal/common_espnow.h"
#include "configuration.h"
#include "./espnow/espnow.h"

PacketHandling &PacketHandling::getInstance() {
    return instance;
}

void PacketHandling::insert(const uint8_t *data, uint8_t len, int8_t rssi) {
    if (len < 2) {
        return; // Need at least packet type and tracker ID
    }

    // Read packet type and tracker ID early for deduplication
    uint8_t packetType = data[0];
    uint8_t trackerId = data[1];

    // FIFO deduplication: Check if this tracker already has data queued
    // Scan existing entries in buffer for same tracker
    for (size_t i = 0; i < buffer.size(); i++) {
        Packet existing = buffer[i];
        if (existing.data[0] == packetType && existing.data[1] == trackerId) {
            // Found existing packet - update in place
            memcpy(existing.data, data, std::min(static_cast<size_t>(len), sizeof(Packet::data)));
            
            // Add RSSI to byte 15 for applicable packet types
            if (packetType != 1 && packetType != 4) {
                existing.data[15] = static_cast<uint8_t>(-rssi);
            }
            
            // Write modified packet back to buffer
            buffer[i] = existing;
            return;
        }
    }

    // No duplicate found - check if buffer has space
    if (buffer.isFull()) {
        droppedReports++;
        Serial.printf("FIFO full! Dropped packet type %d for tracker %d (total dropped: %lu)\n", packetType, trackerId, droppedReports);
        return;
    }

    // Add new entry
    Packet packet;
    memset(packet.data, 0, sizeof(packet.data));
    memcpy(packet.data, data, std::min(static_cast<size_t>(len), sizeof(packet.data)));

    // Add RSSI to byte 15 for applicable packet types
    if (packetType != 1 && packetType != 4) packet.data[15] = static_cast<uint8_t>(-rssi);

    buffer.push(packet);
}

void PacketHandling::insertPriority(const uint8_t *data, uint8_t len) {
    Packet packet;
    memset(packet.data, 0, sizeof(packet.data));
    memcpy(packet.data, data, std::min(static_cast<size_t>(len), sizeof(packet.data)));

    if (priorityBuffer.isFull()) {
        Serial.println("Priority buffer full! Dropping high-priority packet.");
        return;
    }

    priorityBuffer.push(packet);
}

void PacketHandling::sendDisconnectionStatus(uint8_t trackerId) { 
    // Create packet 3 (status) with SVR_STATUS_DISCONNECTED (0)
    uint8_t packet[16] = {0};
    packet[0] = 3;  // packet type 3 (status)
    packet[1] = trackerId;
    packet[2] = 0;  // SVR_STATUS_DISCONNECTED
    packet[3] = 0;  // tracker_status (not relevant for disconnection)
    packet[15] = 0; // RSSI (will be 0 for disconnected tracker)

    //Serial.printf("Sending disconnection status for tracker ID %d\n", trackerId);
    
    insertPriority(packet, 16);
}

void PacketHandling::createRegistrationReport(uint8_t *report, size_t trackerIndex) {
    // Format: [255][tracker_id][6-byte MAC address][8 bytes reserved]
    memset(report, 0, reportSize);
    report[0] = 0xff;
    report[1] = *ESPNowCommunication::getInstance().getTrackerIdByIndex(trackerIndex);
    
    // Get MAC address from connected trackers via ESPNow
    ESPNowCommunication::getInstance().getTrackerMacByIndex(trackerIndex, &report[2]);
    
    // Bytes 8-15 are reserved (already zeroed by memset)
}

void PacketHandling::createRegistrationReport(uint8_t *report, ESPNowCommunication::Tracker tracker) {
    // Format: [255][tracker_id][6-byte MAC address][8 bytes reserved]
    memset(report, 0, reportSize);
    report[0] = 0xff;
    report[1] = tracker.trackerId;
    
    // Copy MAC address
    memcpy(&report[2], tracker.mac.data(), 6);
    
    // Bytes 8-15 are reserved (already zeroed by memset)
}

void PacketHandling::tick(HIDDevice &hidDevice) {
    // PPS print every second (packet types 0-4)
    if (!hidDevice.ready()) return;

    // Throttle to prevent overwhelming USB endpoint
    // unsigned long now = millis();
    // unsigned long interval = now - lastSendAttempt;
    // if (interval < minSendIntervalMs) return;

    //NOTE: This can be expensive if theres a lot of trackers paired, thats why its commented out for now
    // if (now - lastDiscoSweep > 5000) {
    //     //Serial.println("[DISCO] Sending disconnection statuses for unused trackers");
    //     lastDiscoSweep = now;
    //     auto &espnow = ESPNowCommunication::getInstance();
    //     auto pairedIds = Configuration::getInstance().getAllPairedTrackerIds();
    //     for (uint8_t id : pairedIds) {
    //         if (!espnow.isTrackerIdConnected(id)) {
    //             sendDisconnectionStatus(id);
    //         }
    //     }
    // }

    // Prepare 64-byte transfer buffer (4 reports of 16 bytes each), but zeroed out to start
    uint8_t transferBuffer[hidTransferSize];
    memset(transferBuffer, 0, sizeof(transferBuffer));
    size_t reportsWritten = 0;

    // Check to see if theres an available high-priority registration to send
    size_t priorityAvailable = priorityBuffer.size();
    if (priorityAvailable > 0) {
        Packet priorityPacket = priorityBuffer.shift();
        memcpy(&transferBuffer[reportsWritten * reportSize], priorityPacket.data, reportSize);
        reportsWritten++;
        priorityAvailable--;
    }

    // Check if we have any regular packets to send or if we should send a registration report
    size_t availableReports = buffer.size();
    if (availableReports > 0) {
        size_t reportsToSend = std::min(availableReports, reportsPerTransfer - reportsWritten);
        for (size_t i = 0; i < reportsToSend; i++) {
            Packet packet = buffer.shift();
            memcpy(&transferBuffer[reportsWritten * reportSize], packet.data, reportSize);
            reportsWritten++;
        }
    }

    if (reportsWritten < reportsPerTransfer && priorityAvailable > 0) {
        // We have space for more reports and still have high-priority ones waiting - fill remaining space with them
        while (reportsWritten < reportsPerTransfer && priorityAvailable > 0) {
            Packet priorityPacket = priorityBuffer.shift();
            memcpy(&transferBuffer[reportsWritten * reportSize], priorityPacket.data, reportSize);
            reportsWritten++;
            priorityAvailable--;
        }
    }

    // Only send if we have reports to send
    if (reportsWritten > 0 && !hidDevice.send(transferBuffer, hidTransferSize)) Serial.println("[USB] Send failed");
    // Print how long it took to send the reports for debugging
}

PacketHandling PacketHandling::instance;
