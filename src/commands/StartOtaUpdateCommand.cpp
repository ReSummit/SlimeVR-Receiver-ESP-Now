#include "StartOtaUpdateCommand.h"

#include "hal/common_espnow.h"
#include "CommandParsing.h"

bool handleStartOtaUpdateCommand(const String &command) {
    if (!command.startsWith("startotaupdate ")) {
        return false;
    }

    String params = command.substring(15);
    params.trim();

    int firstSpace = params.indexOf(' ');
    if (firstSpace != 32) {
        Serial.println("[CMD] Invalid format. Use: startotaupdate <32hex> <port> <ip> <ssid>\t<password>");
        return true;
    }

    String authStr = params.substring(0, 32);
    uint8_t auth[16];
    if (!parseHexString(authStr, auth, 16)) {
        Serial.println("[CMD] Invalid auth hex string.");
        return true;
    }

    String remaining = params.substring(33);
    int secondSpace = remaining.indexOf(' ');
    if (secondSpace == -1) {
        Serial.println("[CMD] Invalid format. Missing IP address.");
        return true;
    }

    String portStr = remaining.substring(0, secondSpace);
    portStr.trim();
    long portNum = portStr.toInt();
    if (portNum < 1 || portNum > 65535) {
        Serial.println("[CMD] Invalid port number. Use 1-65535.");
        return true;
    }
    uint16_t port = (uint16_t)portNum;

    String ipAndRest = remaining.substring(secondSpace + 1);
    ipAndRest.trim();
    int thirdSpace = ipAndRest.indexOf(' ');
    if (thirdSpace == -1) {
        Serial.println("[CMD] Invalid format. Missing SSID and password.");
        return true;
    }

    String ipStr = ipAndRest.substring(0, thirdSpace);
    String ssidAndPass = ipAndRest.substring(thirdSpace + 1);
    ipStr.trim();
    ssidAndPass.trim();

    uint8_t ip[4];
    if (!parseIpAddress(ipStr, ip)) {
        Serial.println("[CMD] Invalid IP address format.");
        return true;
    }

    int tabIdx = ssidAndPass.indexOf('\t');
    if (tabIdx == -1) {
        Serial.println("[CMD] Invalid format. SSID and password must be separated by a tab character.");
        return true;
    }

    String ssidStr = ssidAndPass.substring(0, tabIdx);
    String passStr = ssidAndPass.substring(tabIdx + 1);
    ssidStr.trim();
    passStr.trim();

    char ssid[33] = {0};
    char password[65] = {0};
    ssidStr.toCharArray(ssid, sizeof(ssid));
    passStr.toCharArray(password, sizeof(password));

    Serial.println("OTAUPDATESTARTED");
    Serial.print("[CMD] OTA Update - Auth: ");
    for (int i = 0; i < 16; i++) {
        Serial.printf("%02x", auth[i]);
    }
    Serial.printf(", Port: %u, IP: %u.%u.%u.%u\n", port, ip[0], ip[1], ip[2], ip[3]);
    Serial.printf("[CMD] SSID: %s\n", ssid);
    Serial.printf("[CMD] Password: %s\n", password);

    ESPNowCommunication::getInstance().startOtaUpdate(auth, port, ip, ssid, password);
    return true;
}