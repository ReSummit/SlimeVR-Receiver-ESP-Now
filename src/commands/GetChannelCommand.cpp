#include "GetChannelCommand.h"

#include "hal/common.h"

bool handleGetChannelCommand(const String &command) {
    if (!command.equalsIgnoreCase("getchannel")) {
        return false;
    }

    int ch = WiFi.channel();
    Serial.printf("[CMD] Current WiFi channel: %d\n", ch);
    return true;
}