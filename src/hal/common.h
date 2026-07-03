#pragma once

#include <Arduino.h>
#include <cstdint>


/**
 * Define WiFi and ESPNow config based on architecture
 */
#if defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266WiFi.h>
#elif defined(ARDUINO_ARCH_ESP32)
#include <WiFi.h>
#include <esp_now.h>
#endif

/**
 * Temperature reading from SOC is not a thing on 8266
 */
#if defined(ARDUINO_ARCH_ESP8266)
static inline float platformTemperature() { return 0.0f; }
#elif defined(ARDUINO_ARCH_ESP32)
static inline float platformTemperature() { return temperatureRead(); }
#endif

/**
 * Pull down on a ESP8266 doesn't have internal pull downs
 * Instead, we'll just set it as an INPUT pin instead
 */
#if defined(ARDUINO_ARCH_ESP8266)
#define INPUT_PULLDOWN INPUT
#endif

/**
 * LittleFS doesn't have a open function with three arguments on 8266
 * Instead, we have to rewrite the reference to a general LittleFS function
 */
#if defined(ARDUINO_ARCH_ESP8266)
#define LittleFS_Platform_Open(path, mode, perm) LittleFS.open(path, mode)
#else
#define LittleFS_Platform_Open(path, mode, perm) LittleFS.open(path, mode, perm)
#endif

/**
 * ESP8266 doesn't allow promiscuous scanning; so it must run on the default channel
 */
#if defined(ARDUINO_ARCH_ESP8266)
#define WiFi_Platform_SetChannel(ichannel) WiFi.channel(ichannel)
#else
#define WiFi_Platform_SetChannel(ichannel) WiFi.setChannel(ichannel)
#endif

/**
 * ESP8266 has missing variables pertaining to ESPNOW
 */
#if defined(ARDUINO_ARCH_ESP8266)

typedef int esp_err_t;

#ifndef ESP_OK
#define ESP_OK 0
#endif

#ifndef ESP_NOW_MAX_DATA_LEN
#define ESP_NOW_MAX_DATA_LEN 250
#endif

#ifndef ESP_ERR_ESPNOW_NO_MEM
#define ESP_ERR_ESPNOW_NO_MEM (-1001)
#endif

#endif