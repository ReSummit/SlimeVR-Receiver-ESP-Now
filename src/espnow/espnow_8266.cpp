#if defined(ARDUINO_ARCH_ESP8266)

#include "configuration.h"
#include "hal/common.h"

#include <ESP8266WiFi.h>
#include <espnow.h>

#include "configuration.h"

#include "packetHandling.h"

#include <string>
#include "../GlobalVars.h"
#include <set>
#include "../serialCom/SerialCom.h"

// Ensure StatusManager type is defined before extern declaration
// Use the global StatusManager instance defined in main.cpp
extern SlimeVR::Status::StatusManager statusManager;


#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#define MAC2ARGS(mac) mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]

static constexpr const char *kPairingCapacityMessage = "Pairing Capacity reached (256 trackers). Unpair trackers before pairing new ones.";


// Static member definition
unsigned int ESPNowCommunication::channel = 6;

ESPNowCommunication ESPNowCommunication::instance;

// Gets the singleton instance of ESPNowCommunication
ESPNowCommunication &ESPNowCommunication::getInstance() {
    return instance;
}

// Adds a callback method for when a tracker is connected
void ESPNowCommunication::onTrackerConnected(std::function<void(Tracker)> callback) {
    trackerConnectedCallbacks.push_back(std::move(callback));
}

// Adds a callback method for when a tracker is disconnected
void ESPNowCommunication::onTrackerDisconnected(std::function<void(Tracker)> callback) {
    trackerDisconnectedCallbacks.push_back(std::move(callback));
}

// Invokes all registered tracker connected event callbacks
void ESPNowCommunication::invokeTrackerConnectedEvent(ESPNowCommunication::Tracker tracker) {
    for (auto &callback : trackerConnectedCallbacks) callback(tracker);
}

// Invokes all registered tracker disconnected event callbacks
void ESPNowCommunication::invokeTrackerDisconnectedEvent(ESPNowCommunication::Tracker tracker) {
    for (auto &callback : trackerDisconnectedCallbacks) callback(tracker);
}

// Gets the number of currently connected trackers
size_t ESPNowCommunication::getConnectedTrackerCount() const {
    return connectedTrackers.size();
}

// Gets the MAC address of a connected tracker by its index
bool ESPNowCommunication::getTrackerMacByIndex(size_t index, uint8_t mac[6]) const {
    if (index >= connectedTrackers.size()) return false;
    memcpy(mac, connectedTrackers[index].mac.data(), 6);
    return true;
}

// Gets the MAC address of a connected tracker by its index
uint8_t* ESPNowCommunication::getTrackerIdByIndex(size_t index) {
    return &connectedTrackers[index].trackerId;
}

// Gets the tracker structure by its index
ESPNowCommunication::Tracker* ESPNowCommunication::getTrackerByIndex(size_t index) {
    if (index >= connectedTrackers.size()) return nullptr;
    return &connectedTrackers[index];
}

// Gets the tracker structure for a given MAC address
ESPNowCommunication::Tracker *ESPNowCommunication::getTracker(const uint8_t peerMac[6]) {
    // Byte-wise compare: peerMac may be unaligned (e.g. a receive-queue slot),
    // and the ESP8266 (lx106) faults on unaligned 32-bit loads (exception 9).
    for (auto &tracker : connectedTrackers) {
        if (memcmp(tracker.mac.data(), peerMac, 6) == 0) return &tracker;
    }
    return nullptr;
}

// Checks if a tracker with the given MAC address is currently connected
bool ESPNowCommunication::isTrackerConnected(const uint8_t peerMac[6]) {
    // Byte-wise compare - see getTracker() for the alignment rationale.
    for (const auto &tracker : connectedTrackers) {
        if (memcmp(tracker.mac.data(), peerMac, 6) == 0 && esp_now_is_peer_exist((uint8_t *) peerMac)) return true;
    }
    return false;
}

// Checks if a tracker ID is currently connected
bool ESPNowCommunication::isTrackerIdConnected(uint8_t trackerId) const {
    for (const auto &tracker : connectedTrackers) if (tracker.trackerId == trackerId) return true;
    return false;
}

// Enters pairing mode
bool ESPNowCommunication::enterPairingMode() {
    if (scanningEnvironment) {
        Serial.println("Cannot enter pairing mode while scanning environment");
        return false;
    }
    if (Configuration::getInstance().isPairedTrackerCapacityReached()) {
        Serial.println(kPairingCapacityMessage);
        return false;
    }
    Serial.println("Entering pairing mode");
    pairing = true;
    pairingStartTime = millis();
    statusManager.setStatus(SlimeVR::Status::PAIRING_MODE, true);

    if (SlimeVR::SerialCom::comEnabled()) {
        SlimeVR::SerialComMessages::PairingMode::print();
    }
    return true;
}

// Exits pairing mode
void ESPNowCommunication::exitPairingMode() {
    Serial.println("Exiting pairing mode");
    pairing = false;
    statusManager.setStatus(SlimeVR::Status::PAIRING_MODE, false);

    if (SlimeVR::SerialCom::comEnabled()) {
        SlimeVR::SerialComMessages::PairingMode::print();
    }
}

// Disconnect a single tracker by MAC
bool ESPNowCommunication::disconnectSingleTracker(const uint8_t mac[6]) {
    for (auto it = connectedTrackers.begin(); it != connectedTrackers.end(); ++it) {
        if (memcmp(it->mac.data(), mac, 6) == 0) {
            uint8_t trackerId = it->trackerId;
            deletePeer(mac);
            Serial.printf("Disconnected tracker %02x:%02x:%02x:%02x:%02x:%02x (ID: %d)\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], trackerId);
            invokeTrackerDisconnectedEvent(*it);
            connectedTrackers.erase(it);
            sendRateUpdateNextTick = true;
            return true;
        }
    }
    return false;
}

// Disconnect all trackers
void ESPNowCommunication::disconnectAllTrackers() {
    for (const auto &tracker : connectedTrackers) {
        disconnectSingleTracker(tracker.mac.data());
    }

    connectedTrackers.clear();
    Serial.println("All trackers disconnected");
}

// Queue a message for sending with rate limiting
void ESPNowCommunication::queueMessageMutex(const uint8_t peerMac[6], const uint8_t *data, size_t dataLen, bool isHeartbeat, bool ephemeral) {
    // Validate message data
    // Serial.printf("Queueing message to " MACSTR " of size %zu\n", MAC2ARGS(peerMac), dataLen);
    if (dataLen == 0 || dataLen > ESP_NOW_MAX_DATA_LEN) {
        Serial.printf("Invalid message size %zu for " MACSTR ", skipping\n", dataLen, MAC2ARGS(peerMac));
        return;
    }

    // Check if queue is full
    size_t nextTail = (queueTail + 1) % maxQueueSize;
    if (nextTail == queueHead) {
        // Calculate queue depth for diagnostic output
        size_t queueDepth = (queueTail >= queueHead) ? (queueTail - queueHead) : (maxQueueSize - queueHead + queueTail);
        Serial.printf("Send queue full! Dropping message to " MACSTR " (queue: %zu/%zu, depth: %zu)\n", MAC2ARGS(peerMac), maxQueueSize, maxQueueSize, queueDepth);
        return;
    }
    
    // Add message to queue
    PendingMessage &msg = sendQueue[queueTail];
    memcpy(msg.peerMac, peerMac, 6);
    memcpy(msg.data, data, dataLen);
    msg.dataLen = dataLen;
    msg.isHeartbeat = isHeartbeat;
    msg.ephemeral = ephemeral;
    msg.skip = false;
    queueTail = nextTail;
}

// Queue a message for sending with rate limiting
void ESPNowCommunication::queueMessage(const uint8_t peerMac[6], const uint8_t *data, size_t dataLen, bool isHeartbeat, bool ephemeral) {
    queueMessageMutex(peerMac, data, dataLen, isHeartbeat, ephemeral);
    processSendQueue();
}

void ESPNowCommunication::queueMessage(const uint8_t peerMac[6], const uint8_t *data, size_t dataLen, bool isHeartbeat) {
    queueMessage(peerMac, data, dataLen, isHeartbeat, false);
}

// Overloaded method to queue a message without tracker pointer
void ESPNowCommunication::queueMessage(const uint8_t peerMac[6], const uint8_t *data, size_t dataLen) {
    queueMessage(peerMac, data, dataLen, false, false);
}

// Process queued messages with rate limiting
void ESPNowCommunication::processSendQueue() {
    if (queueHead == queueTail) return;

    // Serial.printf("Queue in processSendQueue: head=%zu, tail=%zu\n", queueHead, queueTail);

    unsigned long currentTime = millis();
    if (currentTime - lastSendTime >= sendRateLimit) {
        PendingMessage &msg = sendQueue[queueHead];

        if (msg.skip) {
            queueHead = (queueHead + 1) % maxQueueSize;
            return;
        }

        // Validate message data
        if (msg.dataLen == 0 || msg.dataLen > ESP_NOW_MAX_DATA_LEN) {
            Serial.printf("Invalid message size %zu for " MACSTR ", dropping\n", msg.dataLen, MAC2ARGS(msg.peerMac));
            queueHead = (queueHead + 1) % maxQueueSize;
            lastSendTime = currentTime;
            return;
        }

        // Ensure peer is added before sending
        if (!esp_now_is_peer_exist(msg.peerMac)) {
            //Serial.printf("Peer " MACSTR " not found, adding before sending queued message\n", MAC2ARGS(msg.peerMac));
            auto addResult = addPeer(msg.peerMac);
            if (addResult != ESP_OK) {
                Serial.printf("Failed to add peer " MACSTR " for queued message, error: %s (%d)\n", MAC2ARGS(msg.peerMac), espNowErrorToString(addResult).c_str(), addResult);
                queueHead = (queueHead + 1) % maxQueueSize;
                lastSendTime = currentTime;
                return;
            }
        }
        
        //Serial.printf("Sending message to " MACSTR ", size %zu\n", MAC2ARGS(msg.peerMac), msg.dataLen);
        auto result = esp_now_send(msg.peerMac, msg.data, msg.dataLen);
        
        if (msg.ephemeral) {
            // Remove peer if message was ephemeral
            deletePeer(msg.peerMac);
        }

        if (msg.isHeartbeat) {
            // Update ping info if this message is associated with a tracker
            lastHeartbeatCheck = currentTime;
        }

        if (result == ESP_OK) {
            // Message sent successfully, remove from queue
            queueHead = (queueHead + 1) % maxQueueSize;
            lastSendTime = currentTime;
        } else if (result == ESP_ERR_ESPNOW_NO_MEM) {
            // ESP-NOW internal buffer is full - retry this message later without advancing queue
            // Don't update lastSendTime to allow immediate retry on next processSendQueue call
            Serial.printf("ESP-NOW buffer full, retrying message to " MACSTR ", error: %s (%d)\n", MAC2ARGS(msg.peerMac), espNowErrorToString(result).c_str(), result);
        } else {
            // Other errors - log and drop the message
            Serial.printf("Failed to send queued message to " MACSTR ", error: %s (%d)\n", MAC2ARGS(msg.peerMac), espNowErrorToString(result).c_str(), result);
            queueHead = (queueHead + 1) % maxQueueSize;
            lastSendTime = currentTime;
        }
    }
}

// Sends an unpair message to a specific tracker
void ESPNowCommunication::sendUnpairToTracker(const uint8_t mac[6]) {
    ESPNowUnpairMessage unpairMsg;
    memcpy(unpairMsg.securityBytes, securityCode, 8);
	queueMessage(mac, reinterpret_cast<const uint8_t *>(&unpairMsg), sizeof(ESPNowUnpairMessage));
	queueMessage(mac, reinterpret_cast<const uint8_t *>(&unpairMsg), sizeof(ESPNowUnpairMessage));
	queueMessage(mac, reinterpret_cast<const uint8_t *>(&unpairMsg), sizeof(ESPNowUnpairMessage), false, true);
	// Serial.printf("Queued unpair to tracker " MACSTR "\n", MAC2ARGS(mac));
}

// Sends unpair messages to all connected trackers
void ESPNowCommunication::sendUnpairToAllTrackers() {
    ESPNowUnpairMessage unpairMsg;
    memcpy(unpairMsg.securityBytes, securityCode, 8);

    queueMessage(broadcastAddress, reinterpret_cast<const uint8_t *>(&unpairMsg), sizeof(ESPNowUnpairMessage));
    queueMessage(broadcastAddress, reinterpret_cast<const uint8_t *>(&unpairMsg), sizeof(ESPNowUnpairMessage));
    queueMessage(broadcastAddress, reinterpret_cast<const uint8_t *>(&unpairMsg), sizeof(ESPNowUnpairMessage));

    // Serial.println("Unpair messages queued to all trackers");
}

// Sends rate update messages to all connected trackers
void ESPNowCommunication::sendRateUpdateToAllTrackers() {
    size_t trackerCount = connectedTrackers.size();
    if (trackerCount == 0) return; // No trackers to update

    // Calculate polling rate per tracker in Hz: divide maxPPS by number of trackers
    uint32_t pollRateHz = maxPPS / trackerCount;

    Serial.printf("Updating tracker rate: %u trackers, %u Hz per tracker\n", trackerCount, pollRateHz);

    // Queue rate update to all connected trackers
    ESPNowTrackerRateMessage rateMsg;
    rateMsg.pollRateHz = pollRateHz;

    // Send rate update as a broadcast instead of per-tracker
    queueMessage(broadcastAddress, reinterpret_cast<const uint8_t *>(&rateMsg), sizeof(ESPNowTrackerRateMessage));
}

// Initializes ESPNOW communication
ErrorCodes ESPNowCommunication::begin() {
    channel = Configuration::getInstance().getWifiChannel();

    // Pre-allocate vectors to avoid reallocations during operation
    connectedTrackers.reserve(16); // Reserve space for up to 16 trackers
    trackerConnectedCallbacks.reserve(4);
    trackerDisconnectedCallbacks.reserve(4);

    // Load or generate security code
    Configuration::getInstance().getSecurityCode(securityCode);

    WiFi.mode(WIFI_STA);
    WiFi_Platform_SetChannel(channel);
    WiFi.setOutputPower(SLIME_TX_POWER_DBM); // Max power
    WiFi.setPhyMode(WIFI_PHY_MODE_11N);
    wifi_set_sleep_type(NONE_SLEEP_T);

    auto result = esp_now_init();
    if (result != ESP_OK) {
        Serial.printf("Couldn't initialize ESPNOW! - %s\n", espNowErrorToString(result).c_str());
        return ErrorCodes::ESP_NOW_INIT_FAILED;
    }

    // ESP8266 SDK defaults the self role to ESP_NOW_ROLE_IDLE after init, which
    // blocks esp_now_send() (fails with -3). This receiver both sends and
    // receives, so it must run as COMBO. (The ESP32 API has no self-role concept.)
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);

    result = addPeer(broadcastAddress, true);
    if (result != ESP_OK)
    {
        Serial.printf("Couldn't add broadcast peer! - %s\n", espNowErrorToString(result).c_str());
        return ErrorCodes::ESP_NOW_ADDING_BROADCAST_FAILED;
    }

    result = esp_now_register_recv_cb(onReceive);
    if (result != ESP_OK)
    {
        Serial.printf("Couldn't register message callback! - %s\n", espNowErrorToString(result).c_str());
        return ErrorCodes::ESP_RECV_CALLACK_REGISTERING_FAILED;
    }

    uint8_t macaddr[6];
    WiFi.macAddress(macaddr);

    Serial.printf("[ESPNOW] address: %02x:%02x:%02x:%02x:%02x:%02x Channel: %d\n", macaddr[0], macaddr[1], macaddr[2], macaddr[3], macaddr[4], macaddr[5], WiFi.channel());

    if (Configuration::getInstance().wifiChannelFileExists()) {
        Serial.printf("[ESPNOW] Using saved WiFi channel: %d\n", channel);
    } else {
        Serial.println("[ESPNOW] No saved WiFi channel found, scanning environment...");
        enterEnvironmentScanningMode();
    }
    return ErrorCodes::NO_ERROR;
}

// ESPNOW receive callback - runs in the sys (WiFi) context with a tiny stack.
// Do the absolute minimum: copy the frame into the receive queue and return.
// All real work happens later in processReceiveQueue() from update() (cont).
void ESPNowCommunication::onReceive(uint8_t *mac, uint8_t *data, uint8_t dataLen) {
    // Ignore received packets while in scanning mode
    if (ESPNowCommunication::getInstance().isScanningEnvironment()) return;
    ESPNowCommunication::getInstance().enqueueReceived(mac, data, dataLen);
}

// Producer side of the receive ring (sys context). Must not log, allocate, or
// call back into the WiFi stack - just copy bytes and publish the tail.
void ESPNowCommunication::enqueueReceived(const uint8_t *mac, const uint8_t *data, uint8_t dataLen) {
    if (dataLen == 0 || dataLen > receivedMsgMaxLen) return;  // drop empty/oversized (can't Serial here)

    const size_t nextTail = (recvQueueTail + 1) % maxRecvQueueSize;
    if (nextTail == recvQueueHead) return;  // ring full - drop (insert() dedups, so tolerable)

    ReceivedMessage &slot = recvQueue[recvQueueTail];
    memcpy(slot.mac, mac, 6);
    memcpy(slot.data, data, dataLen);
    slot.dataLen = dataLen;

    // Ensure the slot is fully written before the consumer can see the new tail.
    __asm__ volatile("" ::: "memory");
    recvQueueTail = nextTail;
}

// Consumer side of the receive ring (cont context). Drains everything queued
// since the last call and dispatches through the full handleMessage() path.
void ESPNowCommunication::processReceiveQueue() {
    while (recvQueueHead != recvQueueTail) {
        ReceivedMessage &slot = recvQueue[recvQueueHead];
        handleMessage(slot.mac, slot.data, slot.dataLen);
        __asm__ volatile("" ::: "memory");
        recvQueueHead = (recvQueueHead + 1) % maxRecvQueueSize;
    }
}

// Handles incoming ESPNOW messages
void ESPNowCommunication::handleMessage(uint8_t *mac, uint8_t *data, uint8_t dataLen) {
    // Ignore received packets while in scanning mode
    if (ESPNowCommunication::getInstance().isScanningEnvironment()) return;
    // Fast path: cast message once and read header
    //Serial.printf("[ESPNOW] Received message of length %d from " MACSTR "\n", dataLen, MAC2ARGS(senderInfo->src_addr));
    const ESPNowMessage *message = reinterpret_cast<const ESPNowMessage *>(data);
    const ESPNowMessageTypes header = message->base.header;

    // Optimize the most common case - TRACKER_DATA (hot path)
    if (header == ESPNowMessageTypes::TRACKER_DATA) {
        // Fast validation: check if tracker is connected (most packets come from connected trackers)
        Tracker* tracker = getTracker(mac);
        if (tracker == nullptr) return; // Tracker not connected - ignore packet

        // Tracker found and connected - process packet
        recievedPacketCount++;
        recievedByteCount += message->packet.len;

        // Set RSSI to 0 (ESP8266 doesn't have RSSI tracking)
        tracker->rssi = 0;

        tracker->bytesReceived += message->packet.len;
        tracker->packetsReceived += 1;

        // Forward packet to PacketHandling with RSSI
        PacketHandling::getInstance().insert(message->packet.data, message->packet.len, 0);
        return;
    }

    // Handle less frequent message types
    switch (header) {
    case ESPNowMessageTypes::PAIRING_REQUEST: {
        const ESPNowPairingMessage &request = message->pairing;
        if (memcmp(request.securityBytes, securityCode, 8) != 0) return; // Invalid security code

        // Step 1: Check if tracker is already paired
        if (!Configuration::getInstance().isPairedTracker(mac)) {
            if (!pairing) return; // Ignore pairing requests if not in pairing mode
            if (Configuration::getInstance().isPairedTrackerCapacityReached()) {
                Serial.println(kPairingCapacityMessage);
                return;
            }
            Configuration::getInstance().addPairedTracker(mac);
            // Allocate persistent tracker ID for this MAC address
            uint8_t trackerId;
            if (!Configuration::getInstance().getTrackerIdForMac(mac, trackerId)) {
                Configuration::getInstance().removePairedTracker(mac);
                Serial.println(kPairingCapacityMessage);
                return;
            }
            Serial.printf("Paired a new tracker at mac address " MACSTR " with ID %d!\n", MAC2ARGS(mac), trackerId);
        } else {
            Serial.printf("Tracker at mac address " MACSTR " is already paired!\n", MAC2ARGS(mac));
        }

        // Step 2: Send acknowledgment
        ESPNowPairingAckMessage ackMessage;
        // Serial.printf("Sending pairing acknowledgment to " MACSTR "\n", MAC2ARGS(mac));
        queueMessage(mac, reinterpret_cast<uint8_t *>(&ackMessage), sizeof(ackMessage), false, true);

        pairingStartTime = millis();
        break;
    }
    case ESPNowMessageTypes::PAIRING_RESPONSE:
        return;
    case ESPNowMessageTypes::HANDSHAKE_REQUEST: {
        const ESPNowConnectionMessage &handshake = message->connection;
        // Validate security code
        if (memcmp(handshake.securityBytes, securityCode, 8) != 0) {
            // Serial.printf("Received handshake from " MACSTR " with invalid security code! Sent: ", MAC2ARGS(mac));
            // for (int i = 0; i < 8; ++i) Serial.printf("%02x", handshake.securityBytes[i]);
            // Serial.println();
            return;
        }

        // Check that the tracker MAC is in persistent memory
        if (!Configuration::getInstance().isPairedTracker(mac)) {
            Serial.printf("Received handshake from unpaired tracker " MACSTR " - ignoring!\n", MAC2ARGS(mac));
            return;
        }

        Tracker* tracker = getTracker(mac);
        // Check to make sure the tracker isn't already connected
        if (tracker != nullptr) {
            Serial.printf("Tracker at mac address " MACSTR " is already connected!\n", MAC2ARGS(mac));

            ESPNowConnectionAckMessage handshakeResponse;
            handshakeResponse.trackerId = tracker->trackerId;
            handshakeResponse.channel = channel;
            memcpy(handshakeResponse.token, handshake.token, 8);
            memcpy(handshakeResponse.targetAddr, mac, 6);
            Serial.printf("Re-sending handshake ack to " MACSTR " for tracker ID %d - token: ", MAC2ARGS(mac), tracker->trackerId);
            for (int i = 0; i < 8; ++i) Serial.printf("%02x", handshakeResponse.token[i]);
            Serial.println();
            queueMessage(broadcastAddress, reinterpret_cast<const uint8_t *>(&handshakeResponse), sizeof(ESPNowConnectionAckMessage));
            return;
        }

        // Step 1: Get persistent tracker ID for this MAC address
        uint8_t trackerId;
        if (!Configuration::getInstance().getTrackerIdForMac(mac, trackerId)) {
            Serial.println("[ESPNOW] Failed to resolve tracker ID. Tracker capacity may be reached.");
            return;
        }

        // Step 2: Send handshake response with tracker ID and channel
        ESPNowConnectionAckMessage handshakeResponse;
        handshakeResponse.trackerId = trackerId;
        handshakeResponse.channel = channel;
        memcpy(handshakeResponse.token, handshake.token, 8);
        memcpy(handshakeResponse.targetAddr, mac, 6);
        // Serial.printf("Sending handshake ack to " MACSTR " with tracker ID %d\n", MAC2ARGS(mac), trackerId);
        queueMessage(broadcastAddress, reinterpret_cast<const uint8_t *>(&handshakeResponse), sizeof(ESPNowConnectionAckMessage));

        // Step 3: Add tracker to connected list with heartbeat tracking
        Tracker newTracker;
        memcpy(newTracker.mac.data(), mac, 6);
        newTracker.trackerId = trackerId;
        newTracker.waitingForResponse = false;
        newTracker.missedPings = 0;
        newTracker.lastDeltaTime = millis();
        connectedTrackers.push_back(newTracker);

        Serial.printf("Connected tracker " MACSTR " (ID: %d)\n", MAC2ARGS(mac), trackerId);

        uint8_t registrationPacket[16] = {0};
        PacketHandling::getInstance().createRegistrationReport(registrationPacket, newTracker);
        PacketHandling::getInstance().insertPriority(registrationPacket, 16);

        // Step 4: Send rate update to newly connected trackers
        sendRateUpdateNextTick = true;

        // Step 5: Invoke connected event (also sends rate updates to all other trackers)
        invokeTrackerConnectedEvent(newTracker);
        return;
    }
    case ESPNowMessageTypes::HEARTBEAT_ECHO: {
        // Fast MAC lookup for connected tracker
        Tracker *tracker = getTracker(mac);
        if (tracker == nullptr) return;
        tracker->missedPings = 0;

        // Send heartbeat response with the same sequence number
        ESPNowHeartbeatResponseMessage response;
        response.sequenceNumber = message->heartbeatEcho.sequenceNumber;
        // Serial.printf("Sending heartbeat response to tracker " MACSTR " with sequence number %u\n", MAC2ARGS(mac), response.sequenceNumber);
        queueMessage(mac, reinterpret_cast<const uint8_t *>(&response), sizeof(ESPNowHeartbeatResponseMessage));
        return;
    }
    case ESPNowMessageTypes::HEARTBEAT_RESPONSE: {
        // Find the tracker and update heartbeat info
        Tracker *tracker = getTracker(mac);
        if (tracker == nullptr) return;
        if (tracker->waitingForResponse) {
            // Validate sequence number matches expected
            if (message->heartbeatResponse.sequenceNumber == expectedSequenceNumber) {
                unsigned long latency = millis() - lastHeartbeatCheck;
                tracker->latency = static_cast<uint8_t>(latency);
                tracker->waitingForResponse = false;
                tracker->missedPings = 0;
                tracker->rssi = 0;
            }
            // If sequence number doesn't match, ignore the response (likely stale)
        }
        return;
    }
    case ESPNowMessageTypes::ENTER_OTA_ACK:{
        // Find the tracker and mark it as in OTA
        Tracker *tracker = getTracker(mac);
        if (tracker == nullptr) return;

        disconnectSingleTracker(mac);
        return;
    }
    default:
        break;
    }
}

static std::set<uint64_t> channelBSSIDs[12];
static uint32_t channelBytesSeen[12] = {0};

void ESPNowCommunication::scanningLoop() {
    if (!enteredPromiscuousMode) {
        // Initialize scanning state
        memset(channelBytesSeen, 0, sizeof(channelBytesSeen));
        for (int i = 0; i < 12; i++) channelBSSIDs[i].clear();

        if (pairing) exitPairingMode();
        statusManager.setStatus(SlimeVR::Status::SCANNING, true);
        disconnectAllTrackers();

        enteredPromiscuousMode = true;
        scanningTime = 3000;
        scanningChannelStartTime = millis();

        Serial.println("Starting WiFi AP scan for environment scanning (ESP8266)");

        if (SlimeVR::SerialCom::comEnabled()) {
            SlimeVR::SerialComMessages::EnvironmentScanMode::print(scanningTime);
            SlimeVR::SerialComMessages::TrackerUpdate::print(0, 0);
        }

        // WiFi.scanNetworks() scans all channels at once (~2-3 seconds blocking)
        // ESP8266 cannot use promiscuous mode alongside ESP-NOW, so we use
        // the standard AP scan to estimate channel congestion instead.
        int numNetworks = WiFi.scanNetworks(false, true);

        if (numNetworks <= 0) {
            Serial.println("No networks found during scan");
        } else {
            Serial.printf("Found %d networks\n", numNetworks);
        }

        for (int i = 0; i < numNetworks; i++) {
            int ch = WiFi.channel(i);
            int32_t rssi = WiFi.RSSI(i);

            if (ch < 1 || ch > 11) continue;

            // Same RSSI weighting as ESP32 promiscuous scanner
            int multiplier = 1;
            if (rssi >= -20) multiplier = 9;
            else if (rssi >= -30) multiplier = 8;
            else if (rssi >= -40) multiplier = 7;
            else if (rssi >= -50) multiplier = 6;
            else if (rssi >= -60) multiplier = 4;
            else if (rssi >= -70) multiplier = 3;
            else if (rssi >= -80) multiplier = 2;

            // Base score per AP scaled by signal strength
            channelBytesSeen[ch] += 1000 * multiplier;

            // Track unique BSSIDs (only strong signals, matching ESP32 threshold)
            uint8_t* bssid = WiFi.BSSID(i);
            uint64_t bssidKey = 0;
            for (int j = 0; j < 6; j++) {
                bssidKey = (bssidKey << 8) | bssid[j];
            }
            if (rssi >= -70) {
                channelBSSIDs[ch].insert(bssidKey);
            }
        }

        WiFi.scanDelete();

        // Multiply each channel by the number of APs observed (same as ESP32)
        scansRun++;
        for (int ch = 1; ch <= 11; ++ch) {
            size_t uniqueAPs = channelBSSIDs[ch].size();
            channelBytesSeen[ch] *= (1 + uniqueAPs);
        }

        // Emit per-channel progress so the dongle manager gets activity data
        // (on ESP32 these are sent live every second; here they arrive after the scan)
        unsigned long scanElapsed = millis() - scanningChannelStartTime;
        if (SlimeVR::SerialCom::comEnabled()) {
            for (int ch = 1; ch <= 11; ++ch) {
                size_t uniqueAPs = channelBSSIDs[ch].size();
                SlimeVR::SerialComMessages::EnvironmentScanProgress::print(
                    ch, channelBytesSeen[ch], uniqueAPs, scanElapsed, scanningTime);
            }
        }

        // Find the channel with the lowest score
        if (!SlimeVR::SerialCom::comEnabled()) Serial.println("Channel activity summary:");

        uint32_t minBytes = channelBytesSeen[1];
        int bestChannel = 1;
        int primaryChannels[3] = {1, 6, 11};
        int bestPrimary = -1;
        uint32_t minPrimaryBytes = UINT32_MAX;

        for (int ch = 1; ch <= 11; ++ch) {
            size_t uniqueAPs = channelBSSIDs[ch].size();
            if (!SlimeVR::SerialCom::comEnabled()) {
                Serial.printf("Channel %2d: %10u score, %u unique APs\n", ch, channelBytesSeen[ch], uniqueAPs);
            }
            if (channelBytesSeen[ch] < minBytes) {
                minBytes = channelBytesSeen[ch];
                bestChannel = ch;
            }
        }

        // Prefer primary channels (1, 6, 11) if within 10% of best
        for (int i = 0; i < 3; ++i) {
            int ch = primaryChannels[i];
            if (channelBytesSeen[ch] <= minBytes * 1.1) {
                if (channelBytesSeen[ch] < minPrimaryBytes) {
                    minPrimaryBytes = channelBytesSeen[ch];
                    bestPrimary = ch;
                }
            }
        }

        int selected = 0;
        if (bestPrimary != -1) {
            if (!SlimeVR::SerialCom::comEnabled()) Serial.printf("Best channel (primary preferred): %d (score: %u)\n", bestPrimary, channelBytesSeen[bestPrimary]);
            Configuration::getInstance().setWifiChannel((uint8_t)bestPrimary);
            selected = bestPrimary;
        } else {
            if (!SlimeVR::SerialCom::comEnabled()) Serial.printf("Best channel: %d (lowest score: %u)\n", bestChannel, minBytes);
            Configuration::getInstance().setWifiChannel((uint8_t)bestChannel);
            selected = bestChannel;
        }

        delay(100);

        scanningEnvironment = false;
        enteredPromiscuousMode = false;
        statusManager.setStatus(SlimeVR::Status::SCANNING, false);

        if (SlimeVR::SerialCom::comEnabled()) {
            SlimeVR::SerialComMessages::EnvironmentScanResults::print(channelBytesSeen, selected);
            SlimeVR::SerialComMessages::EnvironmentScanMode::print(0);
        }

        // Reinitialize ESP-NOW on the selected channel
        esp_now_deinit();
        begin();
    }
}

// Main update loop to be called regularly
void ESPNowCommunication::update() {
    const unsigned long currentTime = millis();

    // Drain deferred received messages FIRST, every loop, before the throttle
    // below can early-return. This is where handshakes/heartbeats/tracker data
    // are actually handled - in cont context with a full stack.
    processReceiveQueue();

    // Throttle updates to reduce CPU usage - skip if called too frequently
    // This prevents excessive polling when update() is called in a tight loop
    if (currentTime - lastUpdateTime < minUpdateInterval) {
        return;
    }
    lastUpdateTime = currentTime;

    // PRIORITY 1: Handle heartbeat system FIRST - critical for connection stability
    // Process heartbeats before stats/pairing to maintain accurate timing
    if (!connectedTrackers.empty() && (currentTime - lastHeartbeatCheck >= heartbeatInterval)) {
        lastHeartbeatCheck = currentTime;

        //For each connected tracker
        for (auto it = connectedTrackers.begin(); it != connectedTrackers.end();) {
            auto &tracker = *it;

            uint32_t deltaTime = currentTime - tracker.lastDeltaTime;
            tracker.lastDeltaTime = currentTime;
            tracker.bytesPerSecond = (tracker.bytesReceived * 1000) / deltaTime;
            tracker.packetsPerSecond = (tracker.packetsReceived * 1000) / deltaTime;
            tracker.bytesReceived = 0;
            tracker.packetsReceived = 0;

            if (currentTime - tracker.lastRegistrationTime >= registrationIntervalMs) {
                uint8_t registrationPacket[16] = {0};
                PacketHandling::getInstance().createRegistrationReport(registrationPacket, tracker);
                PacketHandling::getInstance().insertPriority(registrationPacket, 16);
                tracker.lastRegistrationTime = currentTime;
            }

            // Check if waiting for response and timeout has occurred
            if (tracker.waitingForResponse) {
                tracker.missedPings++;
                tracker.waitingForResponse = false;
                Serial.printf("Missed heartbeat from tracker " MACSTR " (ID: %d), missed count: %d\n", MAC2ARGS(tracker.mac.data()), tracker.trackerId, tracker.missedPings);

                // Send timed out status on second missed heartbeat
                if (tracker.missedPings == 3) {
                    // Send packet type 3 with SVR_STATUS_TIMED_OUT (2)
                    uint8_t statusPacket[16] = {0};
                    statusPacket[0] = 3; // packet type 3 (status)
                    statusPacket[1] = tracker.trackerId;
                    statusPacket[2] = 5;             // SVR_STATUS_TIMED_OUT
                    statusPacket[3] = 0;             // tracker_status (not relevant for timeout)
                    statusPacket[15] = tracker.rssi; // Use last known RSSI before timeout
                    PacketHandling::getInstance().insert(statusPacket, 16, 0);
                }

                // Remove tracker if exceeded max missed pings
                if (tracker.missedPings >= maxMissedPings)
                {
                    //Serial.printf("Removing tracker " MACSTR " (ID: %d) due to missed heartbeats\n", MAC2ARGS(tracker.mac.data()), tracker.trackerId);
                    disconnectSingleTracker(tracker.mac.data());
                    continue; // Skip increment since we erased
                }
            }

            // Send heartbeat ping if interval has elapsed and not waiting for response
            if (!tracker.waitingForResponse) {
                tracker.waitingForResponse = true;
            } else {
                Serial.printf("WARN: Tracker " MACSTR " (ID: %d) - still waiting for response\n", MAC2ARGS(tracker.mac.data()), tracker.trackerId);
            }

            ++it;
        }

        // Create and send heartbeat echo message with sequence number
        // Serial.printf("Sending heartbeat echo to trackers with sequence number %u\n", heartbeatMsg.sequenceNumber);
        auto lastExpectedSequenceNumber = expectedSequenceNumber;
        expectedSequenceNumber = static_cast<uint16_t>(os_random() & 0xFFFF);
        if (expectedSequenceNumber == lastExpectedSequenceNumber) expectedSequenceNumber = (expectedSequenceNumber + 1) % 0x10000;
        ESPNowHeartbeatEchoMessage heartbeatMsg;
        heartbeatMsg.sequenceNumber = expectedSequenceNumber;
        queueMessage(broadcastAddress, reinterpret_cast<uint8_t *>(&heartbeatMsg), sizeof(ESPNowHeartbeatEchoMessage), true);
        queueMessage(broadcastAddress, reinterpret_cast<uint8_t *>(&heartbeatMsg), sizeof(ESPNowHeartbeatEchoMessage));
        queueMessage(broadcastAddress, reinterpret_cast<uint8_t *>(&heartbeatMsg), sizeof(ESPNowHeartbeatEchoMessage));
        lastHeartbeatCheck = currentTime;
    }

    // Skip lower priority tasks if an OTA update is in progress
    if (ota_in_progress) {
        if (getConnectedTrackerCount() == 0) {
            ota_in_progress = false;
            Serial.println("All trackers entered OTA, resuming normal operation");
            return;
        } else if (currentTime - ota_start_time > ota_timeout) {
            ota_in_progress = false;
            Serial.println("OTA timeout expired, resuming normal operation");
            return;
        } else if (currentTime - ota_last_send_time >= ota_send_interval) {
            ota_last_send_time = currentTime;

            // Send enter OTA command to all connected trackers
            ESPNowEnterOtaModeMessage otaMsg;
            memcpy(otaMsg.securityBytes, securityCode, 8);
            otaMsg.ota_portNum = ota_portNum;
            memcpy(otaMsg.ota_ip, ota_ip, 4);
            memcpy(otaMsg.ota_auth, ota_auth, 16);
            memcpy(otaMsg.ssid, ota_ssid, sizeof(ota_ssid));
            memcpy(otaMsg.password, ota_password, sizeof(ota_password));

            queueMessage(broadcastAddress, reinterpret_cast<uint8_t *>(&otaMsg), sizeof(ESPNowEnterOtaModeMessage));
        }

        // Still report stats during OTA to monitor progress
        if (currentTime - lastStatsReport >= 1000) {
            const int deltaTime = currentTime - lastStatsReport;
            lastStatsReport = currentTime;

            const int pps = (recievedPacketCount * 1000) / deltaTime;
            recievedPacketCount = 0;

            const int bytesPerSecond = (recievedByteCount * 1000) / deltaTime;
            recievedByteCount = 0;

            Serial.printf("T:%d|Q:%d|OTA:1\n", getConnectedTrackerCount(), queueSize());
        }
        return;
    }

    // Skip other tasks if scanning environment
    if (scanningEnvironment) {
        scanningLoop();
        return;
    }

    // PRIORITY 2: Handle pairing announcements - only when in pairing mode
    if (pairing) {
        // Check for pairing mode expiry (60 seconds)
        if (currentTime - pairingStartTime >= 60000) {
            Serial.println("[PAIRING] Pairing mode expired after 60 seconds.");
            exitPairingMode();
        } else if (currentTime - lastPairingBroadcast >= pairingBroadcastInterval) {
            lastPairingBroadcast = currentTime;

            ESPNowPairingAnnouncementMessage announcement;
            announcement.channel = channel;
            memcpy(announcement.securityBytes, securityCode, 8);

            // Serial.println("Broadcasting pairing announcement");
            queueMessage(broadcastAddress, reinterpret_cast<uint8_t *>(&announcement), sizeof(announcement));
        }
    }

    // PRIORITY 3: Print tracker statistics (lowest priority - can be skipped if timing is tight)
    if (currentTime - lastStatsReport >= 1000) {
        const int deltaTime = currentTime - lastStatsReport;
        lastStatsReport = currentTime;

        const int pps = (recievedPacketCount * 1000) / deltaTime;
        recievedPacketCount = 0;

        const int bytesPerSecond = (recievedByteCount * 1000) / deltaTime;
        recievedByteCount = 0;

        // Calculate latency and RSSI stats in single pass
        uint8_t highestLatency = 0;
        unsigned long totalLatency = 0;
        int8_t maxRssi = 0; // Start with minimum possible RSSI
        long totalRssi = 0;
        const size_t trackerCount = connectedTrackers.size();

        for (const auto &tracker : connectedTrackers) {
            const uint8_t lat = tracker.latency;
            totalLatency += lat;
            if (lat > highestLatency) highestLatency = lat;

            const int8_t rssi = tracker.rssi;
            totalRssi += static_cast<long>(rssi); // Ensure signed addition
            if (rssi < maxRssi) maxRssi = rssi;
        }

        const uint8_t avgLatency = trackerCount > 0 ? totalLatency / trackerCount : 0;
        const int8_t avgRssi = trackerCount > 0 ? static_cast<int8_t>(totalRssi / static_cast<long>(trackerCount)) : 0;

        float temp_celsius = platformTemperature();

        if (SlimeVR::SerialCom::comEnabled()) {
            SlimeVR::SerialComMessages::TrackerUpdate::print(bytesPerSecond, pps);
        }

        // Use shorter format to reduce blocking time
        Serial.printf("T:%d|L:%d/%dms|RSSI:%d/%ddBm|PPS:%d|BPS:%d|Temp:%.1fC|SC:%d\n", trackerCount, avgLatency, highestLatency, avgRssi, maxRssi, pps, bytesPerSecond, temp_celsius, SlimeVR::SerialCom::comEnabled());
    }

    // PRIORITY 4: Process send queue - rate limiting to prevent ESP_ERR_ESPNOW_NO_MEM
    processSendQueue();

    // PRIORITY 5: Send rate update if flagged
    if (sendRateUpdateNextTick && (currentTime - lastRateUpdateTime >= 1000)) {
        sendRateUpdateToAllTrackers();
        sendRateUpdateNextTick = false;
        lastRateUpdateTime = currentTime;
    }
}

// Converts ESPNOW error codes to human-readable strings
std::string ESPNowCommunication::espNowErrorToString(esp_err_t error) {
    // ESP8266's espnow.h library doesn't include anything on the different error codes
    switch (error) {
    case ESP_OK:
        return "ESP_OK";
    default:
        return "UNKNOWN_ERROR - " + std::to_string(error);
    }
}

// Adds a ESP-Now peer with the given MAC address
uint8_t ESPNowCommunication::addPeer(const uint8_t peerMac[6], bool defaultConfig) {
    // Check if peer already exists
    if (esp_now_is_peer_exist((uint8_t *) peerMac)) {
        Serial.printf("Peer " MACSTR " already exists.\n", MAC2ARGS(peerMac));
        return ESP_OK; // Peer already exists, return success
    }

    //Serial.printf("Adding peer " MACSTR "\n", MAC2ARGS(peerMac));
    uint8_t channel = wifi_get_channel();
    esp_err_t result = esp_now_add_peer((uint8_t *)peerMac, ESP_NOW_ROLE_CONTROLLER, channel, NULL, 0);
    if (result != ESP_OK) {
        Serial.printf("Failed to add peer, error: %s\n", espNowErrorToString(result).c_str());
    }
    return result;
}

// Adds a ESP-Now peer with the given MAC address (defaultConfig = false)
uint8_t ESPNowCommunication::addPeer(const uint8_t peerMac[6]) {
    return addPeer(peerMac, false);
}

// Deletes a ESP-Now peer with the given MAC address
bool ESPNowCommunication::deletePeer(const uint8_t peerMac[6]) {
    if (!esp_now_is_peer_exist((uint8_t *) peerMac)) {
        Serial.printf("Peer " MACSTR " does not exist.\n", MAC2ARGS((uint8_t *) peerMac));
        return true; // Peer does not exist, return success
    }

    //Serial.printf("Deleting peer " MACSTR "\n", MAC2ARGS((uint8_t *) peerMac));
    auto result = esp_now_del_peer((uint8_t *) peerMac);
    if (result != ESP_OK || esp_now_is_peer_exist((uint8_t *) peerMac)) Serial.printf("Failed to delete peer " MACSTR ", error: %s\n", MAC2ARGS(peerMac), espNowErrorToString(result).c_str());

	//Remove all pending messages to this peer from the send queue by setting the ignore flag
	for (size_t i = 0; i < maxQueueSize; ++i) if (memcmp(sendQueue[i].peerMac, peerMac, 6) == 0) sendQueue[i].skip = true; // Mark message to be skipped

    return result == ESP_OK;
}

void ESPNowCommunication::startOtaUpdate(const uint8_t auth[16], long port, const uint8_t ip[4], const char ssid[33], const char password[65]) {
    memcpy(ota_auth, auth, sizeof(ota_auth));
    ota_portNum = port;
    memcpy(ota_ip, ip, sizeof(ota_ip));
    memcpy(ota_ssid, ssid, sizeof(ota_ssid));
    memcpy(ota_password, password, sizeof(ota_password));
    
    ota_in_progress = true;
    ota_start_time = millis();
}

void ESPNowCommunication::exitEnvironmentScanningMode() {
    scanningEnvironment = false;
    enteredPromiscuousMode = false;
    statusManager.setStatus(SlimeVR::Status::SCANNING, false);
    if (SlimeVR::SerialCom::comEnabled()) {
        SlimeVR::SerialComMessages::EnvironmentScanMode::print(0);
    }
}

void ESPNowCommunication::UnpairAllTrackers() {
    Serial.println("Trackers reset");
    if (SlimeVR::SerialCom::comEnabled()) {
        SlimeVR::SerialComMessages::AllTrackersUnpaired::print();
    }
    Configuration::getInstance().clearAllPairedTrackers();
    sendUnpairToAllTrackers();
    disconnectAllTrackers();
    Configuration::getInstance().resetSecurityCode();

    // Blink LED twice to indicate reset action
    ledManager.pattern(500, 300, 2);
}

#endif