#pragma once

#include "usb_mode.h"

#if USE_USB_HID
#include "HID.h"
#endif

#include "espnow/espnow.h"

#include <Arduino.h>
#include <CircularBuffer.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "Serial.h"

class PacketHandling {
public:
    static PacketHandling &getInstance();

    void insert(const uint8_t *data, uint8_t len, int8_t rssi = 0);
    void insertPriority(const uint8_t *data, uint8_t len);
    void sendDisconnectionStatus(uint8_t trackerId);

    #if USE_USB_HID
    void tick(HIDDevice &hidDevice);
    #else
    void tick();
    #endif
    void createRegistrationReport(uint8_t *report, ESPNowCommunication::Tracker tracker);

private:
    struct Packet {
        uint8_t data[ESPNowCommunication::packetSizeBytes];
    };

    PacketHandling() = default;

    static PacketHandling instance;
    unsigned long lastPpsPrint = 0;

    static constexpr size_t reportSize = 16;  // Each report is 16 bytes
    static constexpr size_t reportsPerTransfer = 4;  // Send 4 reports per USB transfer (64 bytes total)
    static constexpr size_t hidTransferSize = reportSize * reportsPerTransfer;  // 64 bytes total
    static constexpr size_t bufferSize = 64;
    static constexpr unsigned long registrationIntervalMs = 200;  // Only send registrations every 200ms when no data
    static constexpr unsigned long minSendIntervalMs = 0;   // Minimum 1ms between USB transfers
    CircularBuffer<Packet, bufferSize> buffer;
    CircularBuffer<Packet, bufferSize> priorityBuffer;  // Separate buffer for high-priority packets like registrations

    unsigned long lastDiscoSweep = 0;
    
    // Track partial packet transmission
    Packet currentPacket;
    size_t currentChunkIndex = 0;
    bool hasPartialPacket = false;
    
    // Registration system
    unsigned long lastRegistrationSent = 0;
    unsigned long lastSendAttempt = 0;
    size_t nextTrackerIndex = 0;
    
    // Statistics
    unsigned long droppedReports = 0;
    
    void createRegistrationReport(uint8_t *report, size_t trackerIndex);
};
