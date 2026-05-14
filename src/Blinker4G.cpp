#include "Blinker4G.h"

static const char* BLINKER_AUTH_HOST = "iot.diandeng.tech";
static const uint16_t BLINKER_AUTH_PORT = 80;
static const char* BLINKER_AUTH_PATH = "/api/v1/user/device/diy/auth";

static const unsigned long AT_TIMEOUT_LONG = 15000;
static const unsigned long AT_TIMEOUT_MID = 5000;
static const unsigned long AT_TIMEOUT_SHORT = 2000;

// ==================== 构造函数 ====================
Blinker4G::Blinker4G(uint8_t rxPin, uint8_t txPin, uint32_t baud, const char* cmdPassword)
    : _rxPin(rxPin), _txPin(txPin), _baud(baud), _cmdPassword(cmdPassword) {
    _state = STATE_INIT;
    _prevState = STATE_INIT;
    _moduleSerial = nullptr;
    _lastHeartbeat = 0;
    _lastDataReport = 0;
    _lastCredsCheck = 0;
}

// ==================== 初始化 ====================
void Blinker4G::begin(const char* authKey) {
    _authKey = String(authKey);
    _moduleSerial = new SoftwareSerial(_rxPin, _txPin);
    _moduleSerial->begin(_baud);
    
    Serial.println(F("\n[Blinker4G] Starting..."));
    Serial.printf("[Blinker4G] RX:%d TX:%d Baud:%d\n", _rxPin, _txPin, _baud);
    Serial.printf("[Blinker4G] CMD Password: %s\n", _cmdPassword);
    
    changeState(STATE_INIT);
}

// ==================== 回调设置 ====================
void Blinker4G::onData(void (*callback)(const char* data)) { _dataCallback = callback; }
void Blinker4G::onState(void (*callback)(const char* state)) { _stateCallback = callback; }
void Blinker4G::onConnect(void (*callback)()) { _connectCallback = callback; }
void Blinker4G::onDisconnect(void (*callback)()) { _disconnectCallback = callback; }

// ==================== 状态切换 ====================
void Blinker4G::changeState(State newState) {
    static const char* stateNames[] = {
        "INIT", "LOAD_EEPROM", "CONF_HTTP", "GET_CREDS",
        "TRY_MQTT", "CONF_MQTT", "WAIT_CONNECT",
        "CONNECTED", "DISCONNECTED"
    };
    Serial.printf("[Blinker4G] %s -> %s\n", stateNames[_state], stateNames[newState]);
    _prevState = _state;
    _state = newState;
    _stateEnterTime = millis();
}

// ==================== AT 指令 ====================
String Blinker4G::sendAT(const char* cmd, unsigned long timeoutMs) {
    while (_moduleSerial->available()) _moduleSerial->read();
    
    String fullCmd = String(_cmdPassword) + String(cmd);
    _moduleSerial->println(fullCmd);
    
    String response = "";
    unsigned long startMs = millis();
    
    while (millis() - startMs < timeoutMs) {
        while (_moduleSerial->available()) {
            response += (char)_moduleSerial->read();
        }
        if (response.indexOf("OK") >= 0 || response.indexOf("ERROR") >= 0) break;
        delay(10);
    }
    return response;
}

// ==================== 模块初始化 ====================
bool Blinker4G::initModule() {
    Serial.println(F("[Blinker4G] Initializing module..."));
    for (int i = 0; i < 3; i++) {
        String resp = sendAT("AT+VER", AT_TIMEOUT_SHORT);
        if (resp.indexOf("+VER:") >= 0 || resp.indexOf("OK") >= 0) {
            Serial.println(F("[Blinker4G] ✅ Module OK"));
            break;
        }
        if (i == 2) { Serial.println(F("[Blinker4G] ❌ No response")); return false; }
        delay(1000);
    }
    sendAT("AT+CSQ", AT_TIMEOUT_SHORT);
    sendAT("AT+SYSINFO", AT_TIMEOUT_SHORT);
    return true;
}

// ==================== 配置 HTTP 基础参数 ====================
bool Blinker4G::configureHttpBase() {
    Serial.println(F("[Blinker4G] Configuring HTTP base..."));
    char cmd[256];
    
    if (sendAT("AT+WKMOD=HTTPD", AT_TIMEOUT_MID).indexOf("OK") < 0) return false;
    delay(1000);
    if (sendAT("AT+HTPTP=GET", AT_TIMEOUT_SHORT).indexOf("OK") < 0) return false;
    
    sprintf(cmd, "AT+HTPSV=%s,%d", BLINKER_AUTH_HOST, BLINKER_AUTH_PORT);
    if (sendAT(cmd, AT_TIMEOUT_SHORT).indexOf("OK") < 0) return false;
    
    sprintf(cmd, "AT+HTPURL=%s?", BLINKER_AUTH_PATH);
    if (sendAT(cmd, AT_TIMEOUT_SHORT).indexOf("OK") < 0) return false;
    
    sendAT("AT+HTPHD=Accept:text/html[0D][0A]", AT_TIMEOUT_SHORT);
    sendAT("AT+HTPPK=OFF", AT_TIMEOUT_SHORT);
    sendAT("AT+S", AT_TIMEOUT_MID);
    return true;
}

// ==================== 获取凭证 ====================
bool Blinker4G::getCredentials() {
    Serial.println(F("[Blinker4G] Getting credentials..."));
    
    while (_moduleSerial->available()) _moduleSerial->read();
    
    String trigger = "authKey=" + _authKey;
    _moduleSerial->print(trigger);
    
    String response = "";
    unsigned long startMs = millis();
    
    while (millis() - startMs < 15000) {
        while (_moduleSerial->available()) response += (char)_moduleSerial->read();
        if (response.indexOf("detail") >= 0 && response.indexOf('}') >= 0) {
            delay(500);
            while (_moduleSerial->available()) response += (char)_moduleSerial->read();
            break;
        }
        delay(50);
    }
    
    Serial.printf("[Blinker4G] Response (%d bytes): %s\n", response.length(), response.c_str());
    
    int jsonStart = response.indexOf("\r\n\r\n");
    if (jsonStart >= 0) jsonStart += 4;
    else jsonStart = response.indexOf('{');
    if (jsonStart < 0) return false;
    
    String jsonStr = response.substring(jsonStart);
    jsonStr.trim();
    
    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, jsonStr)) return false;
    
    JsonObject detail = doc["detail"];
    if (!detail) return false;
    
    _config.iotId = detail["iotId"] | "";
    _config.iotToken = detail["iotToken"] | "";
    _config.port = detail["port"] | 1883;
    _config.deviceName = detail["deviceName"] | "";
    _config.deviceId = detail["uuid"] | "";
    
    const char* host = detail["host"] | "";
    if (strlen(host) > 0) {
        const char* p = strstr(host, "://");
        _config.broker = String(p ? p + 3 : host);
    } else {
        _config.broker = "broker.diandeng.tech";
    }
    
    _config.valid = (_config.iotId.length() > 0 && _config.iotToken.length() > 0 && _config.deviceId.length() > 0);
    _config.obtainedAt = millis();
    
    if (_config.valid) {
        _config.save();
        Serial.printf("[Blinker4G] ✅ Device: %s\n", _config.deviceId.c_str());
    }
    return _config.valid;
}

// ==================== 配置 MQTT ====================
bool Blinker4G::configureMqtt() {
    Serial.println(F("[Blinker4G] Configuring MQTT..."));
    char cmd[256];
    
    if (sendAT("AT+WKMOD=MQTT,NOR", AT_TIMEOUT_MID).indexOf("OK") < 0) return false;
    delay(500);
    
    sprintf(cmd, "AT+MQTTSVR=%s,%d", _config.broker.c_str(), _config.port);
    sendAT(cmd, AT_TIMEOUT_SHORT);
    
    sprintf(cmd, "AT+MQTTCID=%s", _config.iotId.c_str());
    sendAT(cmd, AT_TIMEOUT_SHORT);
    
    sprintf(cmd, "AT+MQTTUSER=%s", _config.iotId.c_str());
    sendAT(cmd, AT_TIMEOUT_SHORT);
    
    sprintf(cmd, "AT+MQTTPSW=%s", _config.iotToken.c_str());
    sendAT(cmd, AT_TIMEOUT_SHORT);
    
    sprintf(cmd, "AT+MQTTSUBTP=1,1,/device/%s/r,0", _config.deviceName.c_str());
    sendAT(cmd, AT_TIMEOUT_SHORT);
    
    sprintf(cmd, "AT+MQTTPUBTP=1,1,/device/%s/s,0,0", _config.deviceName.c_str());
    sendAT(cmd, AT_TIMEOUT_SHORT);
    
    sendAT("AT+S", AT_TIMEOUT_MID);
    return true;
}

// ==================== 发布 ====================
void Blinker4G::publishData(const char* payload) {
    String msg = "1," + String(payload);
    _moduleSerial->println(msg);
}

void Blinker4G::sendState(const char* state) {
    if (_state != STATE_CONNECTED) return;
    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"toDevice\":\"%s\",\"data\":{\"state\":\"%s\"}}",
             _config.deviceId.c_str(), state);
    publishData(payload);
}

void Blinker4G::sendData(const char* data) {
    if (_state != STATE_CONNECTED) return;
    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"toDevice\":\"%s\",\"data\":{%s}}",
             _config.deviceId.c_str(), data);
    publishData(payload);
}

// ==================== 接收处理 ====================
void Blinker4G::handleIncoming(const char* rawData) {
    // ✅ 去掉主题序号 "1,"
    const char* jsonStart = strchr(rawData, '{');
    if (!jsonStart) return;
    
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, jsonStart)) return;
    
    const char* fromDevice = doc["fromDevice"];
    JsonObject jsonData = doc["data"].as<JsonObject>();
    if (!fromDevice || jsonData.isNull()) return;
    
    if (_dataCallback) {
        String dataStr;
        serializeJson(jsonData, dataStr);
        _dataCallback(dataStr.c_str());
    }
    
    const char* getCmd = jsonData["get"];
    if (getCmd && strcmp(getCmd, "state") == 0) {
        sendState("online");
    }
}

// ==================== 状态查询 ====================
bool Blinker4G::isConnected() { return _state == STATE_CONNECTED; }
const char* Blinker4G::getDeviceId() { return _config.deviceId.c_str(); }
const char* Blinker4G::getDeviceName() { return _config.deviceName.c_str(); }
const char* Blinker4G::getIotId() { return _config.iotId.c_str(); }

// ==================== 工具函数 ====================
void Blinker4G::restartModule() {
    sendAT("AT+Z", AT_TIMEOUT_MID);
    delay(5000);
    initModule();
}

void Blinker4G::factoryReset() {
    sendAT("AT+CLEAR", AT_TIMEOUT_MID);
    delay(5000);
    initModule();
}

void Blinker4G::clearCredentials() {
    Blinker4GConfig::clear();
    _config.valid = false;
}

// ==================== 状态机 ====================
void Blinker4G::processStateMachine() {
    switch (_state) {
        case STATE_INIT:            handleInit(); break;
        case STATE_LOAD_EEPROM:     handleLoadEeprom(); break;
        case STATE_CONFIGURE_HTTP:  handleConfigureHttp(); break;
        case STATE_GET_CREDENTIALS: handleGetCredentials(); break;
        case STATE_TRY_CONNECT_MQTT: handleTryConnectMqtt(); break;
        case STATE_CONFIGURE_MQTT:  handleConfigureMqtt(); break;
        case STATE_WAIT_MQTT_CONNECT: handleWaitMqttConnect(); break;
        case STATE_CONNECTED:       handleConnected(); break;
        case STATE_DISCONNECTED:    handleDisconnected(); break;
    }
}

void Blinker4G::handleInit() {
    if (initModule()) changeState(STATE_LOAD_EEPROM);
    else delay(5000);
}

void Blinker4G::handleLoadEeprom() {
    if (_config.load()) {
        Serial.println(F("[Blinker4G] Found saved credentials"));
        changeState(STATE_TRY_CONNECT_MQTT);
    } else {
        Serial.println(F("[Blinker4G] No saved credentials"));
        changeState(STATE_CONFIGURE_HTTP);
    }
}

void Blinker4G::handleConfigureHttp() {
    if (configureHttpBase()) {
        delay(8000);
        initModule();
        changeState(STATE_GET_CREDENTIALS);
    } else delay(10000);
}

void Blinker4G::handleGetCredentials() {
    if (getCredentials()) changeState(STATE_CONFIGURE_MQTT);
    else delay(10000);
}

void Blinker4G::handleTryConnectMqtt() {
    if (configureMqtt()) {
        delay(8000);
        initModule();
        changeState(STATE_WAIT_MQTT_CONNECT);
    } else {
        _config.valid = false;
        changeState(STATE_CONFIGURE_HTTP);
    }
}

void Blinker4G::handleConfigureMqtt() {
    if (configureMqtt()) {
        delay(8000);
        initModule();
        changeState(STATE_WAIT_MQTT_CONNECT);
    } else changeState(STATE_GET_CREDENTIALS);
}

void Blinker4G::handleWaitMqttConnect() {
    unsigned long elapsed = millis() - _stateEnterTime;
    if (elapsed > 30000) {
        if (_prevState == STATE_TRY_CONNECT_MQTT) {
            Serial.println(F("[Blinker4G] EEPROM credentials failed"));
            Blinker4GConfig::clear();
            _config.valid = false;
            changeState(STATE_CONFIGURE_HTTP);
        } else changeState(STATE_GET_CREDENTIALS);
    } else if (elapsed > 15000) {
        _lastHeartbeat = millis() - 30000;
        _lastCredsCheck = millis();
        changeState(STATE_CONNECTED);
        if (_connectCallback) _connectCallback();
    }
}

void Blinker4G::handleConnected() {
    // 心跳
    if (millis() - _lastHeartbeat > 30000) {
        _lastHeartbeat = millis();
        sendState("online");
    }
    
    // ✅ 每 60 秒检查一次 MQTT 连接状态
    if (millis() - _lastCredsCheck > 60000) {
        _lastCredsCheck = millis();
        String resp = sendAT("AT+MQTTSTA", AT_TIMEOUT_SHORT);
        Serial.printf("[Blinker4G] MQTT status: %s\n", resp.c_str());
        
        // 明确返回 DISCONNECTION 才判定断连
        if (resp.indexOf("\"DISCONNECTION\"") >= 0) {
            Serial.println(F("[Blinker4G] ⚠️ MQTT disconnected!"));
            changeState(STATE_DISCONNECTED);
            return;
        }
    }
    
    // 接收数据
    while (_moduleSerial->available()) {
        String line = _moduleSerial->readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
            Serial.printf("[RAW] %s\n", line.c_str());
            handleIncoming(line.c_str());
        }
    }
    
    // 凭证过期
    if (millis() - _config.obtainedAt > 3600000) {
        Serial.println(F("[Blinker4G] Credentials expired"));
        changeState(STATE_DISCONNECTED);
    }
}

void Blinker4G::handleDisconnected() {
    if (_disconnectCallback) _disconnectCallback();
    _config.valid = false;
    changeState(STATE_GET_CREDENTIALS);
}

void Blinker4G::run() {
    processStateMachine();
    delay(100);
}