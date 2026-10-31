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
    _lastCredsCheck = 0;
    _recvBuffer = "";
}

// ==================== 初始化 ====================
void Blinker4G::begin(const char* authKey) {
    _authKey = String(authKey);
    resetFrame();
    _queueHead = _queueCount = 0;
    for (auto& message : _pending) message = "";
    _retryPending = false;
    _statusFailures = 0;
    _atLine = "";
    _httpBody = "";
    if (_ownsSerial) {
        if (_softwareSerial) {
            delete _softwareSerial;
        }
        _softwareSerial = new SoftwareSerial(_rxPin, _txPin);
        _softwareSerial->begin(_baud, SWSERIAL_8N1, _rxPin, _txPin, false,
                               BLINKER4G_SOFT_RX_SIZE, BLINKER4G_SOFT_ISR_SIZE);
        _moduleSerial = _softwareSerial;
    }

    // Sharing UART0 with the module must never inject log text into its input.
    if (Blinker4GLog::output() == static_cast<Print*>(_moduleSerial)) {
        setLogStream(nullptr);
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
    if (_state == STATE_CONNECTED && newState != STATE_CONNECTED) ++_quality.disconnects;
    if (_state != STATE_CONNECTED && newState == STATE_CONNECTED) {
        if (_hasConnected) ++_quality.reconnects;
        _hasConnected = true;
        _connectedAt = millis();
        _lastQualityReportAt = millis();
    }
    _prevState = _state;
    _state = newState;
    _stateEnterTime = millis();
    _retryPending = false;

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
    if (!_moduleSerial || _atActive) return "";
    pollIncoming();
    waitWithReceive(BLINKER4G_TX_GAP_MS);
    _atLine = "";
    _lineOverflow = false;
    _atResponse = "";
    _atDone = false;
    _atActive = true;
    writeFrame(String(_cmdPassword) + cmd);
    unsigned long startMs = millis();
    while (!_atDone && millis() - startMs < timeoutMs) {
        pollIncoming();
        delay(1);
    }
    _atActive = false;
    if (!_atDone) ++_quality.atTimeouts;
    // Never expose echoed MQTT passwords through debug logs.
    BLINKER4G_LOG_DEBUG("AT", "Response: %u bytes, complete=%d", _atResponse.length(), _atDone);
    return _atDone ? _atResponse : String();
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
        waitWithReceive(1000);
    }

    sampleSignalQuality();
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

    pollIncoming();
    resetFrame();
    _httpBody = "";
    _captureHttp = true;
    writeFrame("authKey=" + _authKey);
    unsigned long startMs = millis();
    while (_httpBody.length() == 0 && millis() - startMs < Blinker4GModuleDriver::AT_TIMEOUT_LONG) {
        pollIncoming();
        delay(1);
    }
    _captureHttp = false;
    if (!_driver.parseCredentialsResponse(_httpBody, _config)) {
        _httpBody = "";
        BLINKER4G_LOG_ERROR("AUTH", "Failed to parse credential response");
        return false;
    }
    _httpBody = "";

    if (_config.valid) {
        if (!_config.save(_authKey.c_str())) {
            BLINKER4G_LOG_ERROR("AUTH", "Could not persist credentials; using RAM copy");
        }
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
    writeFrame(_driver.buildPublishFrame(payload));
}

bool Blinker4G::receiveEnabled() const {
#if BLINKER4G_RX_POLICY == BLINKER4G_RX_POLICY_FORCE_OFF
    return false;
#elif BLINKER4G_RX_POLICY == BLINKER4G_RX_POLICY_FORCE_ON
    return true;
#else
    return true;
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

Blinker4G::ConnectionQuality Blinker4G::getConnectionQuality() const {
    ConnectionQuality quality = _quality;
    quality.connected = _state == STATE_CONNECTED;
    quality.connectedForMs = quality.connected ? millis() - _connectedAt : 0;
    quality.signalAgeMs = quality.hasSignalSample ? millis() - _signalSampleAt : 0;
    quality.rssiDbm = quality.csq >= 0 && quality.csq <= 31 ? quality.csq * 2 - 113 : 0;
    quality.rxDrops = _droppedFrames;
    quality.serialOverflows = _serialOverflows;
    return quality;
}

bool Blinker4G::sampleSignalQuality() {
    String response = sendAT("AT+CSQ", Blinker4GModuleDriver::AT_TIMEOUT_SHORT);
    int csq, ber;
    _quality.lastSignalQueryOk = _driver.parseSignalQualityResponse(response, csq, ber);
    if (_quality.lastSignalQueryOk) {
        _quality.csq = csq;
        _quality.ber = ber;
        _quality.hasSignalSample = true;
        _signalSampleAt = millis();
    }
    return _quality.lastSignalQueryOk;
}

bool Blinker4G::refreshConnectionQuality() {
    if (_state != STATE_CONNECTED || _atActive) return false;
    return sampleSignalQuality();
}

void Blinker4G::setConnectionQualityReportInterval(unsigned long intervalMs) {
    _qualityReportInterval = intervalMs && intervalMs < 30000UL ? 30000UL : intervalMs;
    _lastQualityReportAt = millis();
}

bool Blinker4G::reportConnectionQuality() {
    if (_state != STATE_CONNECTED || _atActive) return false;
    _lastQualityReportAt = millis();
    refreshConnectionQuality();
    ConnectionQuality quality = getConnectionQuality();
    StaticJsonDocument<768> doc;
    JsonObject connection = doc.createNestedObject("connection");
    connection["connected"] = quality.connected;
    connection["signalQueryOk"] = quality.lastSignalQueryOk;
    connection["csq"] = nullptr;
    connection["ber"] = nullptr;
    connection["rssiDbm"] = nullptr;
    connection["signalAgeMs"] = nullptr;
    if (quality.hasSignalSample) {
        connection["csq"] = quality.csq;
        connection["ber"] = quality.ber;
        connection["signalAgeMs"] = quality.signalAgeMs;
        if (quality.csq >= 0 && quality.csq <= 31) connection["rssiDbm"] = quality.rssiDbm;
    }
    connection["connectedForMs"] = quality.connectedForMs;
    connection["reconnects"] = quality.reconnects;
    connection["disconnects"] = quality.disconnects;
    connection["atTimeouts"] = quality.atTimeouts;
    connection["rxDrops"] = quality.rxDrops;
    connection["serialOverflows"] = quality.serialOverflows;
    String fields;
    serializeJson(doc, fields);
    fields = fields.substring(1, fields.length() - 1);
    sendData(fields.c_str());
    return true; // Written to the module, not an MQTT delivery acknowledgement.
}

// ==================== 接收处理 ====================
void Blinker4G::handleIncoming(const char* rawData) {
    const char* jsonStart = strchr(rawData, '{');
    if (!jsonStart) return;

    BLINKER4G_LOG_DEBUG("RX", "%s", rawData);

    DynamicJsonDocument doc(BLINKER4G_RX_FRAME_SIZE * 2);
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

void Blinker4G::resetFrame() {
    _recvBuffer = "";
    _braceDepth = 0;
    _inString = _escaped = _discardFrame = false;
}

void Blinker4G::pollIncoming() {
    if (!_moduleSerial) return;
    if (_softwareSerial && _softwareSerial->overflow()) {
        ++_serialOverflows;
        ++_droppedFrames;
        resetFrame();
        _atLine = "";
        _lineOverflow = false;
    }
    if (_braceDepth && millis() - _lastRxAt >= BLINKER4G_RX_TIMEOUT_MS) {
        ++_droppedFrames;
        resetFrame();
    }
    while (_moduleSerial->available()) {
        char ch = (char)_moduleSerial->read();
        if (_braceDepth || ch == '{') {
            if (_braceDepth == 0) {
                // Discard a DTU topic/index prefix, not a part of the AT reply.
                _atLine = "";
                _lineOverflow = false;
            }
            _lastRxAt = millis();
            if (!_discardFrame) {
                if (_recvBuffer.length() < BLINKER4G_RX_FRAME_SIZE) _recvBuffer += ch;
                else { _discardFrame = true; ++_droppedFrames; }
            }
            if (_escaped) _escaped = false;
            else if (_inString && ch == '\\') _escaped = true;
            else if (ch == '"') _inString = !_inString;
            else if (!_inString) {
                if (ch == '{') ++_braceDepth;
                else if (ch == '}') --_braceDepth;
            }
            if (_braceDepth == 0) {
                if (!_discardFrame) {
                    if (_captureHttp) {
                        if (_httpBody.length() == 0) _httpBody = _recvBuffer;
                    } else if (receiveEnabled()) {
                        if (_queueCount < BLINKER4G_RX_QUEUE_SIZE) {
                            _pending[(_queueHead + _queueCount) % BLINKER4G_RX_QUEUE_SIZE] = _recvBuffer;
                            ++_queueCount;
                        } else ++_droppedFrames;
                    }
                }
                resetFrame();
            }
            continue;
        }
        if (ch == '\n') {
            _atLine.trim();
            if (_atActive && !_atDone && !_lineOverflow && _atLine.length()) {
                if (_atResponse.length() + _atLine.length() + 2 <= 1536) {
                    _atResponse += _atLine;
                    _atResponse += "\r\n";
                }
                if (_atLine == "OK" || _atLine == "ERROR" ||
                    _atLine.startsWith("+ERR:") || _atLine.startsWith("+CME ERROR:")) {
                    _atDone = true;
                }
            }
            _atLine = "";
            _lineOverflow = false;
        } else if (ch != '\r' && !_lineOverflow) {
            if (_atLine.length() < 256) _atLine += ch;
            else _lineOverflow = true;
        }
    }
}

void Blinker4G::dispatchIncoming() {
    if (_state != STATE_CONNECTED || _atActive) return;
    // Never invoke user callbacks inside a serial read/AT transaction.
    uint8_t remaining = _queueCount;
    while (remaining-- && _queueCount) {
        String frame = _pending[_queueHead];
        _pending[_queueHead] = "";
        _queueHead = (_queueHead + 1) % BLINKER4G_RX_QUEUE_SIZE;
        --_queueCount;
        handleIncoming(frame.c_str());
    }
}

void Blinker4G::waitWithReceive(unsigned long duration) {
    unsigned long start = millis();
    do {
        pollIncoming();
        delay(1);
    } while (millis() - start < duration);
}

void Blinker4G::writeFrame(const String& frame) {
    if (!_moduleSerial) return;
    // A newline alone is not the DTU's packet boundary. Respect UARTFT.
    while (_hasWritten && millis() - _lastWriteAt < BLINKER4G_TX_GAP_MS) {
        pollIncoming();
        delay(1);
    }
    for (size_t i = 0; i < frame.length(); ++i) {
        _moduleSerial->write((uint8_t)frame[i]);
        pollIncoming();
        if ((i & 15) == 15) yield();
    }
    _moduleSerial->print("\r\n");
    _moduleSerial->flush();
    _lastWriteAt = millis();
    _hasWritten = true;
    pollIncoming();
}

void Blinker4G::retryLater(unsigned long duration) {
    _retryAt = millis() + duration;
    _retryPending = true;
}

bool Blinker4G::isConnected() { return _state == STATE_CONNECTED; }
const char* Blinker4G::getDeviceId() { return _config.deviceId.c_str(); }
const char* Blinker4G::getDeviceName() { return _config.deviceName.c_str(); }
const char* Blinker4G::getIotId() { return _config.iotId.c_str(); }

// ==================== 工具函数 ====================
void Blinker4G::restartModule() {
    sendAT("AT+Z", Blinker4GModuleDriver::AT_TIMEOUT_MID);
    waitWithReceive(5000);
    changeState(STATE_INIT);
}

void Blinker4G::factoryReset() {
    clearCredentials();
    sendAT("AT+CLEAR", Blinker4GModuleDriver::AT_TIMEOUT_MID);
    waitWithReceive(5000);
    changeState(STATE_INIT);
}

void Blinker4G::clearCredentials() {
    Blinker4GConfig::clear();
    _config = Blinker4GConfig();
    changeState(STATE_CONFIGURE_HTTP);
}

// ==================== 状态机 ====================
void Blinker4G::processStateMachine() {
    if (_retryPending) {
        if ((int32_t)(millis() - _retryAt) < 0) return;
        _retryPending = false;
    }
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
    else retryLater(5000);
}

void Blinker4G::handleLoadEeprom() {
    if (_config.load(_authKey.c_str())) {
        BLINKER4G_LOG_INFO("AUTH", "Found saved credentials");
        changeState(STATE_TRY_CONNECT_MQTT);
    } else {
        BLINKER4G_LOG_INFO("AUTH", "No saved credentials");
        changeState(STATE_CONFIGURE_HTTP);
    }
}

void Blinker4G::handleConfigureHttp() {
    if (configureHttpBase()) {
        _queueHead = _queueCount = 0;
        for (auto& message : _pending) message = "";
        waitWithReceive(Blinker4GModuleDriver::MQTT_BOOT_SETTLE_MS);
        if (!initModule()) { retryLater(5000); return; }
        changeState(STATE_GET_CREDENTIALS);
    } else {
        retryLater(10000);
    }
}

void Blinker4G::handleGetCredentials() {
    if (getCredentials()) changeState(STATE_CONFIGURE_MQTT);
    else retryLater(10000);
}

void Blinker4G::handleTryConnectMqtt() {
    if (configureMqtt()) {
        _lastCredsCheck = 0;
        waitWithReceive(Blinker4GModuleDriver::MQTT_BOOT_SETTLE_MS);
        if (!initModule()) { retryLater(5000); return; }
        changeState(STATE_WAIT_MQTT_CONNECT);
    } else {
        _config.valid = false;
        changeState(STATE_CONFIGURE_HTTP);
    }
}

void Blinker4G::handleConfigureMqtt() {
    if (configureMqtt()) {
        _lastCredsCheck = 0;
        waitWithReceive(Blinker4GModuleDriver::MQTT_BOOT_SETTLE_MS);
        if (!initModule()) { retryLater(5000); return; }
        changeState(STATE_WAIT_MQTT_CONNECT);
    } else {
        changeState(STATE_CONFIGURE_HTTP);
        retryLater(5000);
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
        _statusFailures = 0;
        changeState(STATE_CONNECTED);
        return;
    }

    if (elapsed > Blinker4GModuleDriver::MQTT_CONNECT_TIMEOUT_MS) {
        BLINKER4G_LOG_ERROR("MQTT", "Reconnect failed; switching to HTTP authentication");
        _config.valid = false;
        changeState(STATE_CONFIGURE_HTTP);
        retryLater(5000);
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
            // One missed AT response must not invalidate a healthy connection.
            if (++_statusFailures >= 2) {
                changeState(STATE_DISCONNECTED);
                return;
            }
            _lastCredsCheck = millis() - Blinker4GModuleDriver::MQTT_STATUS_CHECK_MS + 3000;
        } else _statusFailures = 0;
    }

    pollIncoming();
    if (_qualityReportInterval && millis() - _lastQualityReportAt >= _qualityReportInterval) {
        reportConnectionQuality();
    }
}

void Blinker4G::handleDisconnected() {
    _statusFailures = 0;
    changeState(_config.valid ? STATE_TRY_CONNECT_MQTT : STATE_CONFIGURE_HTTP);
    retryLater(3000);
}

void Blinker4G::run() {
    pollIncoming();
    dispatchIncoming();
    processStateMachine();
    pollIncoming();
    dispatchIncoming();
    delay(1);
}
