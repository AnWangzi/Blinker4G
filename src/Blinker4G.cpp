#include "Blinker4G.h"

// ==================== 构造函数 ====================
Blinker4G::Blinker4G(uint8_t rxPin, uint8_t txPin, uint32_t baud, const char* cmdPassword)
    : _rxPin(rxPin), _txPin(txPin), _baud(baud), _cmdPassword(cmdPassword) {
    _state = STATE_INIT;
    _prevState = STATE_INIT;
    _moduleSerial = nullptr;
    _softwareSerial = nullptr;
    _ownsSerial = true;
    _lastHeartbeat = 0;
    _lastDataReport = 0;
    _lastCredsCheck = 0;
    _recvBuffer = "";
}

Blinker4G::Blinker4G(Stream& serial, const char* cmdPassword)
    : _rxPin(0), _txPin(0), _baud(0), _cmdPassword(cmdPassword) {
    _state = STATE_INIT;
    _prevState = STATE_INIT;
    _moduleSerial = &serial;
    _softwareSerial = nullptr;
    _ownsSerial = false;
    _lastHeartbeat = 0;
    _lastDataReport = 0;
    _lastCredsCheck = 0;
    _recvBuffer = "";
}

// ==================== 初始化 ====================
void Blinker4G::begin(const char* authKey) {
    _authKey = String(authKey);
    if (_ownsSerial) {
        if (_softwareSerial) {
            delete _softwareSerial;
        }
        _softwareSerial = new SoftwareSerial(_rxPin, _txPin);
        _softwareSerial->begin(_baud);
        _moduleSerial = _softwareSerial;
    }

    BLINKER4G_LOG_INFO("CORE", "Starting");
    if (_ownsSerial) {
        BLINKER4G_LOG_INFO("CORE", "RX:%d TX:%d Baud:%d", _rxPin, _txPin, _baud);
        BLINKER4G_LOG_INFO("CORE", "Serial backend: SoftwareSerial");
    } else {
        BLINKER4G_LOG_INFO("CORE", "Serial backend: external Stream");
    }
    BLINKER4G_LOG_INFO("CORE", "Module: %s", _driver.name());
#if BLINKER4G_ENABLE_DEBUG
    BLINKER4G_LOG_INFO("CORE", "Debug logging: enabled");
#else
    BLINKER4G_LOG_INFO("CORE", "Debug logging: disabled");
#endif
    BLINKER4G_LOG_INFO("CORE", "RX policy: %s", receiveEnabled() ? "enabled" : "disabled");

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

    BLINKER4G_LOG_INFO("STATE", "%s -> %s", stateNames[_state], stateNames[newState]);
    _prevState = _state;
    _state = newState;
    _stateEnterTime = millis();

    if (_stateCallback) {
        _stateCallback(stateNames[newState]);
    }
    if (newState == STATE_CONNECTED && _connectCallback) {
        _connectCallback();
    }
    if (newState == STATE_DISCONNECTED && _disconnectCallback) {
        _disconnectCallback();
    }
}

// ==================== AT 指令 ====================
String Blinker4G::sendAT(const char* cmd, unsigned long timeoutMs) {
    if (!_moduleSerial) return "";

    while (_moduleSerial->available()) _moduleSerial->read();

    String fullCmd = String(_cmdPassword) + String(cmd);
    BLINKER4G_LOG_DEBUG("AT", ">>> %s", cmd);
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
    if (response.length() == 0) {
        BLINKER4G_LOG_DEBUG("AT", "<<< <no response>");
    } else {
        response.replace("\r", "\\r");
        response.replace("\n", "\\n");
        BLINKER4G_LOG_DEBUG("AT", "<<< %s", response.c_str());
    }
    return response;
}

// ==================== 模块初始化 ====================
bool Blinker4G::initModule() {
    BLINKER4G_LOG_INFO("CORE", "Initializing module");
    for (int i = 0; i < 3; i++) {
        String resp = sendAT("AT+VER", Blinker4GModuleDriver::AT_TIMEOUT_SHORT);
        if (resp.indexOf("+VER:") >= 0 || resp.indexOf("OK") >= 0) {
            BLINKER4G_LOG_INFO("CORE", "Module OK");
            break;
        }
        if (i == 2) {
            BLINKER4G_LOG_ERROR("CORE", "Module did not respond");
            return false;
        }
        delay(1000);
    }

    sendAT("AT+CSQ", Blinker4GModuleDriver::AT_TIMEOUT_SHORT);
    sendAT("AT+SYSINFO", Blinker4GModuleDriver::AT_TIMEOUT_SHORT);
    return true;
}

// ==================== 配置 HTTP 基础参数 ====================
bool Blinker4G::configureHttpBase() {
    BLINKER4G_LOG_INFO("HTTP", "Configuring HTTP base");
    return _driver.configureHttpBase(*this);
}

// ==================== 获取凭证 ====================
bool Blinker4G::getCredentials() {
    BLINKER4G_LOG_INFO("AUTH", "Requesting device credentials");

    while (_moduleSerial->available()) _moduleSerial->read();

    String trigger = "authKey=" + _authKey;
    _moduleSerial->println(trigger);

    String response = "";
    unsigned long startMs = millis();

    while (millis() - startMs < Blinker4GModuleDriver::AT_TIMEOUT_LONG) {
        while (_moduleSerial->available()) response += (char)_moduleSerial->read();
        if (response.indexOf("detail") >= 0 && response.indexOf('}') >= 0) {
            delay(500);
            while (_moduleSerial->available()) response += (char)_moduleSerial->read();
            break;
        }
        delay(50);
    }

    BLINKER4G_LOG_DEBUG("AUTH", "Response length: %d", response.length());

    if (!_driver.parseCredentialsResponse(response, _config)) {
        BLINKER4G_LOG_ERROR("AUTH", "Failed to parse credential response");
        return false;
    }

    if (_config.valid) {
        _config.save();
        BLINKER4G_LOG_INFO("AUTH", "Credentials ready for device %s", _config.deviceId.c_str());
    }
    return _config.valid;
}

// ==================== 配置 MQTT ====================
bool Blinker4G::configureMqtt() {
    BLINKER4G_LOG_INFO("MQTT", "Configuring MQTT");
    return _driver.configureMqtt(*this, _config);
}

// ==================== 发布 ====================
void Blinker4G::publishData(const char* payload) {
    if (!_moduleSerial) return;
    BLINKER4G_LOG_INFO("TX", "%s", payload);
    _moduleSerial->println(_driver.buildPublishFrame(payload));
}

bool Blinker4G::receiveEnabled() const {
#if BLINKER4G_RX_POLICY == BLINKER4G_RX_POLICY_FORCE_OFF
    return false;
#elif BLINKER4G_RX_POLICY == BLINKER4G_RX_POLICY_FORCE_ON
    return true;
#else
#if BLINKER4G_PLATFORM_ESP8266
    return !_ownsSerial;
#else
    return true;
#endif
#endif
}

void Blinker4G::sendState(const char* state) {
    if (_state != STATE_CONNECTED) return;
    String payload = _driver.buildStatePayload(_config, state);
    publishData(payload.c_str());
}

void Blinker4G::sendData(const char* data) {
    if (_state != STATE_CONNECTED) return;
    String payload = _driver.buildDataPayload(_config, data);
    publishData(payload.c_str());
}

// ==================== 接收处理 ====================
void Blinker4G::handleIncoming(const char* rawData) {
    const char* jsonStart = strchr(rawData, '{');
    if (!jsonStart) return;

    BLINKER4G_LOG_DEBUG("RX", "%s", rawData);

    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, jsonStart)) {
        BLINKER4G_LOG_ERROR("RX", "Failed to parse incoming payload");
        return;
    }

    JsonObject jsonData = doc["data"].as<JsonObject>();
    if (jsonData.isNull()) {
        BLINKER4G_LOG_ERROR("RX", "Incoming message does not contain data field");
        return;
    }

    if (_dataCallback) {
        String dataStr;
        serializeJson(jsonData, dataStr);
        BLINKER4G_LOG_INFO("RX", "%s", dataStr.c_str());
        _dataCallback(dataStr.c_str());
    }

    const char* getCmd = jsonData["get"];
    if (getCmd && strcmp(getCmd, "state") == 0) {
        sendState("online");
    }
}

void Blinker4G::pollIncoming() {
    if (!receiveEnabled()) {
        while (_moduleSerial && _moduleSerial->available()) {
            _moduleSerial->read();
        }
        _recvBuffer = "";
        return;
    }

    while (_moduleSerial && _moduleSerial->available()) {
        char ch = (char)_moduleSerial->read();

        if (_recvBuffer.length() == 0) {
            if (ch == '{') {
                _recvBuffer += ch;
            }
            continue;
        }

        _recvBuffer += ch;

        if (_recvBuffer.length() > 512) {
            BLINKER4G_LOG_ERROR("RX", "Buffered frame exceeded 512 bytes, dropping");
            _recvBuffer = "";
            continue;
        }

        int braceDepth = 0;
        bool inString = false;
        bool escaped = false;
        for (size_t i = 0; i < _recvBuffer.length(); ++i) {
            char current = _recvBuffer[i];
            if (escaped) {
                escaped = false;
                continue;
            }
            if (current == '\\') {
                escaped = true;
                continue;
            }
            if (current == '"') {
                inString = !inString;
                continue;
            }
            if (inString) continue;
            if (current == '{') braceDepth++;
            if (current == '}') braceDepth--;
        }

        if (braceDepth == 0) {
            BLINKER4G_LOG_DEBUG("RX", "Buffered frame complete: %s", _recvBuffer.c_str());
            handleIncoming(_recvBuffer.c_str());
            _recvBuffer = "";
        }
    }
}

// ==================== 状态查询 ====================
bool Blinker4G::isConnected() { return _state == STATE_CONNECTED; }
const char* Blinker4G::getDeviceId() { return _config.deviceId.c_str(); }
const char* Blinker4G::getDeviceName() { return _config.deviceName.c_str(); }
const char* Blinker4G::getIotId() { return _config.iotId.c_str(); }

// ==================== 工具函数 ====================
void Blinker4G::restartModule() {
    sendAT("AT+Z", Blinker4GModuleDriver::AT_TIMEOUT_MID);
    delay(5000);
    initModule();
}

void Blinker4G::factoryReset() {
    sendAT("AT+CLEAR", Blinker4GModuleDriver::AT_TIMEOUT_MID);
    delay(5000);
    initModule();
}

void Blinker4G::clearCredentials() {
    Blinker4GConfig::clear();
    _config = Blinker4GConfig();
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
        BLINKER4G_LOG_INFO("AUTH", "Found saved credentials");
        changeState(STATE_TRY_CONNECT_MQTT);
    } else {
        BLINKER4G_LOG_INFO("AUTH", "No saved credentials");
        changeState(STATE_CONFIGURE_HTTP);
    }
}

void Blinker4G::handleConfigureHttp() {
    if (configureHttpBase()) {
        delay(Blinker4GModuleDriver::MQTT_BOOT_SETTLE_MS);
        initModule();
        changeState(STATE_GET_CREDENTIALS);
    } else {
        delay(10000);
    }
}

void Blinker4G::handleGetCredentials() {
    if (getCredentials()) changeState(STATE_CONFIGURE_MQTT);
    else delay(10000);
}

void Blinker4G::handleTryConnectMqtt() {
    if (configureMqtt()) {
        _lastCredsCheck = 0;
        delay(Blinker4GModuleDriver::MQTT_BOOT_SETTLE_MS);
        initModule();
        changeState(STATE_WAIT_MQTT_CONNECT);
    } else {
        _config.valid = false;
        changeState(STATE_CONFIGURE_HTTP);
    }
}

void Blinker4G::handleConfigureMqtt() {
    if (configureMqtt()) {
        _lastCredsCheck = 0;
        delay(Blinker4GModuleDriver::MQTT_BOOT_SETTLE_MS);
        initModule();
        changeState(STATE_WAIT_MQTT_CONNECT);
    } else {
        changeState(STATE_GET_CREDENTIALS);
    }
}

void Blinker4G::handleWaitMqttConnect() {
    unsigned long elapsed = millis() - _stateEnterTime;
    if (elapsed < Blinker4GModuleDriver::MQTT_BOOT_SETTLE_MS) {
        return;
    }

    if (millis() - _lastCredsCheck < Blinker4GModuleDriver::MQTT_STATUS_POLL_MS) {
        return;
    }

    _lastCredsCheck = millis();
    String resp = sendAT("AT+MQTTSTA", Blinker4GModuleDriver::AT_TIMEOUT_SHORT);
    BLINKER4G_LOG_DEBUG("MQTT", "Status response: %s", resp.c_str());

    if (_driver.isMqttConnectedResponse(resp)) {
        _lastHeartbeat = millis() - Blinker4GModuleDriver::HEARTBEAT_INTERVAL_MS;
        _lastCredsCheck = millis();
        changeState(STATE_CONNECTED);
        return;
    }

    if (elapsed > Blinker4GModuleDriver::MQTT_CONNECT_TIMEOUT_MS) {
        if (_prevState == STATE_TRY_CONNECT_MQTT) {
            BLINKER4G_LOG_ERROR("MQTT", "Saved credentials failed, fallback to HTTP auth");
            Blinker4GConfig::clear();
            _config.valid = false;
            changeState(STATE_CONFIGURE_HTTP);
        } else {
            changeState(STATE_GET_CREDENTIALS);
        }
    }
}

void Blinker4G::handleConnected() {
    if (millis() - _lastHeartbeat > Blinker4GModuleDriver::HEARTBEAT_INTERVAL_MS) {
        _lastHeartbeat = millis();
        sendState("online");
    }

    if (millis() - _lastCredsCheck > Blinker4GModuleDriver::MQTT_STATUS_CHECK_MS) {
        _lastCredsCheck = millis();
        String resp = sendAT("AT+MQTTSTA", Blinker4GModuleDriver::AT_TIMEOUT_SHORT);
        BLINKER4G_LOG_DEBUG("MQTT", "Status response: %s", resp.c_str());

        if (!_driver.isMqttConnectedResponse(resp)) {
            if (millis() - _config.obtainedAt > Blinker4GModuleDriver::CREDENTIAL_REFRESH_MS) {
                BLINKER4G_LOG_INFO("MQTT", "Credentials expired, refreshing");
            } else {
                BLINKER4G_LOG_INFO("MQTT", "Disconnected, reconnecting");
            }
            changeState(STATE_DISCONNECTED);
            return;
        }
    }

    pollIncoming();
}

void Blinker4G::handleDisconnected() {
    _config.valid = false;
    changeState(STATE_GET_CREDENTIALS);
}

void Blinker4G::run() {
    processStateMachine();
    delay(100);
}