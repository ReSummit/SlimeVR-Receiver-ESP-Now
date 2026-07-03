#if defined(ARDUINO_ARCH_ESP8266)

#pragma once

#include "error_codes.h"
#include "espnow/messages.h"
#include "hal/common.h"

#include <cstdint>
#include <functional>
#include <vector>
#include "Serial.h"

class ESPNowCommunication {
    public:
        // Heartbeat tracking structure
        struct Tracker {
            std::array<uint8_t, 6> mac;
            uint8_t trackerId;
            bool waitingForResponse = false;
            uint8_t missedPings = 0;
            uint8_t latency = 0;
            int8_t rssi = 0;  // Signal strength in dBm
            uint16_t bytesReceived = 0; // Total bytes received from this tracker
            uint16_t packetsReceived = 0; // Total packets received from this tracker
            uint16_t bytesPerSecond = 0; // Calculated bytes per second
            uint16_t packetsPerSecond = 0; // Calculated packets per second
            uint32_t lastDeltaTime = 0; // Time since pps and bps were last calculated, used for accurate rate calculations
            uint32_t lastRegistrationTime = 0; // Timestamp of when the tracker was registered
        };
        
        // NOTE: To reduce RAM on 8266, this needed to be modified from 128 to 32
        static constexpr size_t packetSizeBytes = 32;

        static unsigned int channel;

        const static unsigned int maxPPS = 1500; // Maximum packets per second total across all trackers

        static ESPNowCommunication &getInstance();

        ErrorCodes begin();

        bool enterPairingMode();
        void exitPairingMode();
        bool isInPairingMode() const { return pairing; }
        
        void disconnectAllTrackers();
        bool disconnectSingleTracker(const uint8_t mac[6]);
        void sendUnpairToAllTrackers();
        void sendUnpairToTracker(const uint8_t mac[6]);
        bool isTrackerIdConnected(uint8_t trackerId) const;

        void update();

        void onTrackerConnected(std::function<void(Tracker)> callback);
        void onTrackerDisconnected(std::function<void(Tracker)> callback);  // Passes tracker structure
        
        size_t getConnectedTrackerCount() const;
        bool getTrackerMacByIndex(size_t index, uint8_t mac[6]) const;
        uint8_t* getTrackerIdByIndex(size_t index);
        Tracker* getTrackerByIndex(size_t index);
        uint8_t securityCode[8];

        bool isTrackerConnected(const uint8_t peerMac[6]);

        void startOtaUpdate(const uint8_t auth[16], long port, const uint8_t ip[4], const char ssid[33], const char password[65]);

        void enterEnvironmentScanningMode() { 
            Serial.println("Wifi Promiscuous mode scanning not supported on ESP8266");
            scanningEnvironment = false;
        }
        void exitEnvironmentScanningMode();
        bool isScanningEnvironment() const { return scanningEnvironment; }
        void UnpairAllTrackers();

        Tracker* getTracker(const uint8_t peerMac[6]);

    private:
        static ESPNowCommunication instance;
        ESPNowCommunication() = default;

        void invokeTrackerConnectedEvent(Tracker tracker);
        void invokeTrackerDisconnectedEvent(Tracker tracker);
        void sendRateUpdateToAllTrackers();

        static void onReceive(uint8_t *mac, uint8_t *data, uint8_t dataLen);
        // NOTE: no 'flatten' here. On ESP8266 the recv callback runs in the tiny
        // SDK/WiFi (sys) stack; flattening inlined Serial.printf/esp_now_send into
        // one giant frame and overflowed it. Messages are now deferred to cont
        // context via the receive queue below, and handleMessage keeps a normal
        // (non-flattened) frame.
        void handleMessage(uint8_t *mac, uint8_t *data, uint8_t dataLen);

        // Deferred receive queue. The ESP8266 esp_now recv callback runs in the
        // sys context with a very small stack and must not call esp_now_send or
        // do heavy work (Serial, malloc, ...). onReceive() only copies the raw
        // frame here; processReceiveQueue() drains it from update() (cont context)
        // where there is a real stack. Single-producer (sys) / single-consumer
        // (cont) lock-free ring: producer touches only tail, consumer only head.
        static constexpr uint8_t receivedMsgMaxLen = 96;  // > any received message (tracker payload caps at 32)
        static constexpr size_t maxRecvQueueSize = 16;
        struct ReceivedMessage {
            uint8_t mac[6];
            uint8_t dataLen;
            uint8_t data[receivedMsgMaxLen];
        };
        ReceivedMessage recvQueue[maxRecvQueueSize];
        volatile size_t recvQueueHead = 0;  // consumer (cont) only
        volatile size_t recvQueueTail = 0;  // producer (sys) only
        void enqueueReceived(const uint8_t *mac, const uint8_t *data, uint8_t dataLen);
        void processReceiveQueue();

        uint8_t addPeer(const uint8_t peerMac[6]);
        uint8_t addPeer(const uint8_t peerMac[6], bool defaultConfig);
        bool deletePeer(const uint8_t peerMac[6]);

        bool pairing = false;

        bool sendRateUpdateNextTick = false;
        unsigned long lastRateUpdateTime = 0;

        unsigned int recievedPacketCount = 0;
        unsigned int recievedByteCount = 0;
        unsigned long lastStatsReport = 0;
        
        // Store connected tracker MAC addresses with heartbeat tracking
        std::vector<Tracker> connectedTrackers;
        
        static constexpr unsigned long heartbeatInterval = 1000; // 1 second
        static constexpr uint8_t maxMissedPings = 5;
        uint16_t expectedSequenceNumber = 0;

        std::vector<std::function<void(Tracker)>> trackerConnectedCallbacks;
        std::vector<std::function<void(Tracker)>> trackerDisconnectedCallbacks;

        static constexpr uint8_t broadcastAddress[6]{0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
        static constexpr uint8_t espnowWifiChannel = 6;

        unsigned long lastPairingBroadcast = 0;
        unsigned long pairingStartTime = 0;
        unsigned long lastHeartbeatCheck = 0;
        unsigned long lastUpdateTime = 0;
        static constexpr unsigned long pairingBroadcastInterval = 100;
        static constexpr unsigned long minUpdateInterval = 10;  // Minimum 10ms between update() calls

        // Send queue for rate limiting
        struct PendingMessage {
            uint8_t peerMac[6];
            uint8_t data[ESP_NOW_MAX_DATA_LEN];
            size_t dataLen;
            bool ephemeral;
            bool isHeartbeat;
            bool skip = false;
        };

        // NOTE: To reduce RAM on 8266, this needed to be modified from 64 to 16
        static constexpr size_t maxQueueSize = 16;
        PendingMessage sendQueue[maxQueueSize];
        size_t queueHead = 0;
        size_t queueTail = 0;
        int queueSize() const {
            return queueHead == queueTail ? 0 : (queueHead > queueTail ? ((maxQueueSize - queueHead) + queueTail) : (queueTail - queueHead));
        }
        unsigned long lastSendTime = 0;
        static constexpr unsigned long sendRateLimit = 5;
        void queueMessageMutex(const uint8_t peerMac[6], const uint8_t *data, size_t dataLen, bool isHeartbeat, bool ephemeral);
        void queueMessage(const uint8_t peerMac[6], const uint8_t *data, size_t dataLen, bool isHeartbeat, bool ephemeral);
        void queueMessage(const uint8_t peerMac[6], const uint8_t *data, size_t dataLen, bool isHeartbeat);
        void queueMessage(const uint8_t peerMac[6], const uint8_t *data, size_t dataLen);
        void processSendQueue();

        std::string espNowErrorToString(esp_err_t error);

        uint8_t ota_auth[16];
        long ota_portNum;
        uint8_t ota_ip[4];
        char ota_ssid[33];
        char ota_password[65];

        unsigned int ota_timeout = 10000U; // 10 seconds timeout for OTA, trackers shouldn't take too long to enter this mode
        unsigned int ota_send_interval = 2000U; // Resend every 2 seconds

        bool ota_in_progress = false;
        unsigned long ota_start_time = 0;
        unsigned long ota_last_send_time = 0;

        bool scanningEnvironment = false;
        bool enteredPromiscuousMode = false;
        long unsigned long scanningChannelStartTime = 0;
        int scanningTime = 0;
        long unsigned int scanningChannelDuration = 5000; // 5 seconds per channel
        void scanningLoop();
        int scansRun = 0;

        static constexpr unsigned long registrationIntervalMs = 500; // 0.5 second

};

#endif