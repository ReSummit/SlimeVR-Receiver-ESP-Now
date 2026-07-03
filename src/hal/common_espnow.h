#pragma once

#include <Arduino.h>
#include <cstdint>

/**
 * ESPNow on ESP8266 operates differently compared to ESP32, 
 * so we need to switch between implementations here.
 */
#if defined(ARDUINO_ARCH_ESP8266)
#include "espnow/espnow_8266.h"
#elif defined(ARDUINO_ARCH_ESP32)
#include "espnow/espnow.h"
#endif