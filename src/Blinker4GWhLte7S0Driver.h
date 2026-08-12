#ifndef BLINKER4G_WH_LTE_7S0_DRIVER_H
#define BLINKER4G_WH_LTE_7S0_DRIVER_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "Blinker4GConfig.h"

class Blinker4G;

class Blinker4GWhLte7S0Driver {
public:
    static constexpr unsigned long AT_TIMEOUT_LONG = 15000;
    static constexpr unsigned long AT_TIMEOUT_MID = 5000;
    static constexpr unsigned long AT_TIMEOUT_SHORT = 2000;
    static constexpr unsigned long MQTT_BOOT_SETTLE_MS = 8000;
    static constexpr unsigned long MQTT_CONNECT_TIMEOUT_MS = 30000;
    static constexpr unsigned long MQTT_STATUS_POLL_MS = 1000;
    static constexpr unsigned long MQTT_STATUS_CHECK_MS = 60000;
    static constexpr unsigned long HEARTBEAT_INTERVAL_MS = 30000;
    static constexpr unsigned long CREDENTIAL_REFRESH_MS = 3600000;

    const char* name() const;
    const char* authHost() const;
    uint16_t authPort() const;
    const char* authPath() const;

    bool configureHttpBase(Blinker4G& owner) const;
    bool configureMqtt(Blinker4G& owner, const Blinker4GConfig& config) const;
    bool parseCredentialsResponse(const String& response, Blinker4GConfig& config) const;
    bool isMqttConnectedResponse(const String& response) const;
    bool extractIncomingPayload(const char* rawData, String& payload) const;
    String buildPublishFrame(const char* payload) const;
    String buildStatePayload(const Blinker4GConfig& config, const char* state) const;
    String buildDataPayload(const Blinker4GConfig& config, const char* data) const;

private:
    bool sendChecked(Blinker4G& owner, const char* command, unsigned long timeoutMs) const;
};

#endif