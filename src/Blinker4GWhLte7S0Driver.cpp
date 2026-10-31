#include "Blinker4GWhLte7S0Driver.h"
#include "Blinker4G.h"

static const char* BLINKER_AUTH_HOST = "iot.diandeng.tech";
static const uint16_t BLINKER_AUTH_PORT = 80;
static const char* BLINKER_AUTH_PATH = "/api/v1/user/device/diy/auth";

const char* Blinker4GWhLte7S0Driver::name() const { return "WH-LTE-7S0"; }
const char* Blinker4GWhLte7S0Driver::authHost() const { return BLINKER_AUTH_HOST; }
uint16_t Blinker4GWhLte7S0Driver::authPort() const { return BLINKER_AUTH_PORT; }
const char* Blinker4GWhLte7S0Driver::authPath() const { return BLINKER_AUTH_PATH; }

bool Blinker4GWhLte7S0Driver::sendChecked(Blinker4G& owner, const char* command, unsigned long timeoutMs) const {
    return owner.sendAT(command, timeoutMs).endsWith("OK\r\n");
}

bool Blinker4GWhLte7S0Driver::configureHttpBase(Blinker4G& owner) const {
    char command[256];

    if (!sendChecked(owner, "AT+WKMOD=HTTPD", AT_TIMEOUT_MID)) return false;
    if (!sendChecked(owner, "AT+HTPTP=GET", AT_TIMEOUT_SHORT)) return false;

    snprintf(command, sizeof(command), "AT+HTPSV=%s,%u", authHost(), authPort());
    if (!sendChecked(owner, command, AT_TIMEOUT_SHORT)) return false;

    snprintf(command, sizeof(command), "AT+HTPURL=%s?", authPath());
    if (!sendChecked(owner, command, AT_TIMEOUT_SHORT)) return false;

    if (!sendChecked(owner, "AT+HTPHD=Accept:text/html[0D][0A]", AT_TIMEOUT_SHORT)) return false;
    if (!sendChecked(owner, "AT+HTPPK=OFF", AT_TIMEOUT_SHORT)) return false;
    if (!sendChecked(owner, "AT+S", AT_TIMEOUT_MID)) return false;
    BLINKER4G_LOG_DEBUG("HTTP", "WH-LTE-7S0 HTTP profile configured");
    return true;
}

bool Blinker4GWhLte7S0Driver::configureMqtt(Blinker4G& owner, const Blinker4GConfig& config) const {
    char command[256];

    if (!sendChecked(owner, "AT+WKMOD=MQTT,NOR", AT_TIMEOUT_MID)) return false;
    if (!sendChecked(owner, "AT+MQTTCFG=60,0", AT_TIMEOUT_SHORT)) return false;
    if (!sendChecked(owner, "AT+MQTTMOD=1", AT_TIMEOUT_SHORT)) return false;

    snprintf(command, sizeof(command), "AT+MQTTSVR=%s,%d", config.broker.c_str(), config.port);
    if (!sendChecked(owner, command, AT_TIMEOUT_SHORT)) return false;

    snprintf(command, sizeof(command), "AT+MQTTCID=%s", config.iotId.c_str());
    if (!sendChecked(owner, command, AT_TIMEOUT_SHORT)) return false;

    snprintf(command, sizeof(command), "AT+MQTTUSER=%s", config.iotId.c_str());
    if (!sendChecked(owner, command, AT_TIMEOUT_SHORT)) return false;

    snprintf(command, sizeof(command), "AT+MQTTPSW=%s", config.iotToken.c_str());
    if (!sendChecked(owner, command, AT_TIMEOUT_SHORT)) return false;

    snprintf(command, sizeof(command), "AT+MQTTSUBTP=1,1,/device/%s/r,0", config.deviceName.c_str());
    if (!sendChecked(owner, command, AT_TIMEOUT_SHORT)) return false;

    snprintf(command, sizeof(command), "AT+MQTTPUBTP=1,1,/device/%s/s,0,0", config.deviceName.c_str());
    if (!sendChecked(owner, command, AT_TIMEOUT_SHORT)) return false;

    if (!sendChecked(owner, "AT+S", AT_TIMEOUT_MID)) return false;
    BLINKER4G_LOG_DEBUG("MQTT", "WH-LTE-7S0 MQTT profile configured for %s:%d", config.broker.c_str(), config.port);
    return true;
}

bool Blinker4GWhLte7S0Driver::parseCredentialsResponse(const String& response, Blinker4GConfig& config) const {
    int jsonStart = response.indexOf('{');
    if (jsonStart < 0) {
        BLINKER4G_LOG_ERROR("AUTH", "HTTP response does not contain JSON body");
        return false;
    }

    String jsonStr = response.substring(jsonStart);
    jsonStr.trim();

    DynamicJsonDocument doc(BLINKER4G_RX_FRAME_SIZE * 2);
    if (deserializeJson(doc, jsonStr)) {
        BLINKER4G_LOG_ERROR("AUTH", "Credential JSON decode failed");
        return false;
    }

    JsonObject detail = doc["detail"];
    if (detail.isNull()) {
        BLINKER4G_LOG_ERROR("AUTH", "Credential JSON missing detail field");
        return false;
    }

    config.iotId = detail["iotId"] | "";
    config.iotToken = detail["iotToken"] | "";
    config.port = detail["port"] | 1883;
    config.deviceName = detail["deviceName"] | "";
    config.deviceId = detail["uuid"] | "";

    const char* host = detail["host"] | "";
    if (strlen(host) > 0) {
        const char* scheme = strstr(host, "://");
        config.broker = String(scheme ? scheme + 3 : host);
        int path = config.broker.indexOf('/');
        if (path >= 0) config.broker = config.broker.substring(0, path);
        int colon = config.broker.indexOf(':');
        if (colon >= 0) config.broker = config.broker.substring(0, colon);
    } else {
        config.broker = "broker.diandeng.tech";
    }

    config.valid = config.complete();
    config.obtainedAt = millis();
    if (!config.valid) {
        BLINKER4G_LOG_ERROR("AUTH", "Credential payload is incomplete");
    }
    return config.valid;
}

bool Blinker4GWhLte7S0Driver::isMqttConnectedResponse(const String& response) const {
    int start = response.indexOf("+MQTTSTA:");
    if (start < 0 || !response.endsWith("OK\r\n")) return false;
    int end = response.indexOf('\n', start);
    String status = response.substring(start + 9, end);
    status.trim();
    return status == "\"CONNECTION\"" || status == "CONNECTION";
}

bool Blinker4GWhLte7S0Driver::extractIncomingPayload(const char* rawData, String& payload) const {
    if (!rawData) return false;

    const char* jsonStart = strchr(rawData, '{');
    if (!jsonStart) return false;

    DynamicJsonDocument doc(BLINKER4G_RX_FRAME_SIZE * 2);
    if (deserializeJson(doc, jsonStart)) return false;

    JsonObject jsonData = doc["data"].as<JsonObject>();
    if (jsonData.isNull()) return false;

    payload = "";
    serializeJson(jsonData, payload);
    return true;
}

String Blinker4GWhLte7S0Driver::buildPublishFrame(const char* payload) const {
    String frame;
    frame.reserve(strlen(payload) + 2);
    frame += "1,";
    frame += payload;
    return frame;
}

String Blinker4GWhLte7S0Driver::buildStatePayload(const Blinker4GConfig& config, const char* state) const {
    String payload;
    payload.reserve(config.deviceId.length() + strlen(state) + 40);
    payload += "{\"toDevice\":\"";
    payload += config.deviceId;
    payload += "\",\"data\":{\"state\":\"";
    payload += state;
    payload += "\"}}";
    return payload;
}

String Blinker4GWhLte7S0Driver::buildDataPayload(const Blinker4GConfig& config, const char* data) const {
    String payload;
    payload.reserve(config.deviceId.length() + strlen(data) + 40);
    payload += "{\"toDevice\":\"";
    payload += config.deviceId;
    payload += "\",\"data\":{";
    payload += data;
    payload += "}}";
    return payload;
}
