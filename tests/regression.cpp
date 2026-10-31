#include "Arduino.h"
#include <ArduinoJson.h>
#include <deque>
#include <functional>
#include <vector>
#include <cassert>
#include <iostream>
#define private public
#include "Blinker4G.h"
#undef private

NullSerial Serial;
FakeEEPROM EEPROM;
uint32_t fakeMillis = 0;

class FakeStream : public Stream {
public:
    std::deque<char> input;
    std::string line;
    std::vector<std::string> writes;
    std::function<void(const std::string&)> reply;
    std::deque<std::pair<uint32_t, std::string>> scheduled;
    void feed(const std::string& data) { for (char c : data) input.push_back(c); }
    void later(uint32_t ms, const std::string& data) { scheduled.emplace_back(fakeMillis + ms, data); }
    int available() override {
        while (!scheduled.empty() && int32_t(fakeMillis - scheduled.front().first) >= 0) {
            feed(scheduled.front().second);
            scheduled.pop_front();
        }
        return int(input.size());
    }
    int read() override { char c = input.front(); input.pop_front(); return uint8_t(c); }
    size_t write(uint8_t c) override {
        if (c == '\n') {
            writes.push_back(line);
            std::string command = line;
            line.clear();
            if (reply) reply(command);
        } else if (c != '\r') line += char(c);
        return 1;
    }
};

static std::vector<std::string> received;
static Blinker4G* callbackOwner;
static bool acknowledge = false;
static void onData(const char* data) {
    assert(!callbackOwner->_atActive);
    received.push_back(data);
    if (acknowledge) callbackOwner->sendData("\"ack\":true");
}
static void ready(Blinker4G& b) {
    b._state = Blinker4G::STATE_CONNECTED;
    b._stateEnterTime = millis();
    b._lastHeartbeat = b._lastCredsCheck = millis();
    b._config.broker = "broker.example";
    b._config.iotId = "iot";
    b._config.iotToken = "token";
    b._config.deviceId = "uuid";
    b._config.deviceName = "name";
    b._config.valid = true;
    callbackOwner = &b;
    b.onData(onData);
    received.clear();
}

static void receiveAndAt() {
    FakeStream stream;
    Blinker4G b(stream);
    ready(b);
    stream.feed("1,{\"data\":{\"text\":\"brace } and \\\"quote\\\"\",\"n\":1");
    b.pollIncoming();
    assert(received.empty());
    stream.feed("}}{\"data\":{\"n\":2}}");
    stream.reply = [&](const std::string& command) {
        if (command.find("AT+") == std::string::npos) return;
        stream.feed("\r\n{\"data\":{\"text\":\"OK ERROR\",\"n\":3}}\r\n"
                    "+MQTTSTA:\"CONNECTION\"\r\n\r\nOK\r\n");
    };
    String response = b.sendAT("AT+MQTTSTA", 100);
    assert(b._driver.isMqttConnectedResponse(response));
    assert(response.indexOf("\"data\"") < 0);
    assert(received.empty());
    acknowledge = true;
    b.dispatchIncoming();
    acknowledge = false;
    assert(received.size() == 3);
    assert(received[0].find("\"n\":1") != std::string::npos);
    assert(received[2].find("\"n\":3") != std::string::npos);
    received.clear();
    stream.reply = [&](const std::string& command) {
        if (command.find("AT+") == std::string::npos) return;
        stream.feed("{\"data\":{\"text\":\"OK ERROR");
        stream.later(5, "\",\"n\":4}}\r\n+MQTTSTA:\"CONNECTION\"\r\nOK\r\n");
    };
    assert(b._driver.isMqttConnectedResponse(b.sendAT("AT+MQTTSTA", 100)));
    b.dispatchIncoming();
    assert(received.size() == 1 && received[0].find("\"n\":4") != std::string::npos);
    // A word in echoed parameters must not terminate an AT transaction.
    stream.reply = [&](const std::string&) { stream.feed("echo OK in parameter\r\n"); };
    assert(b.sendAT("AT+TEST", 10).length() == 0);
    assert(!b._driver.isMqttConnectedResponse("+MQTTSTA:NOTCONNECTION\r\nOK\r\n"));
    std::cout << "PASS AT demultiplexing, escaped/concatenated JSON, callback publish, exact terminators\n";
}

static void recoveryAndBounds() {
    FakeStream stream;
    Blinker4G b(stream);
    ready(b);
    stream.feed("{\"data\":{");
    b.pollIncoming();
    fakeMillis += BLINKER4G_RX_TIMEOUT_MS + 1;
    stream.feed("{\"data\":{\"ok\":1}}");
    b.pollIncoming();
    b.dispatchIncoming();
    assert(received.size() == 1 && b.droppedFrames() == 1);
    stream.feed("{\"data\":{\"large\":\"" + std::string(1200, 'x') + "\"}}{\"data\":{\"ok\":2}}");
    b.pollIncoming();
    b.dispatchIncoming();
    assert(received.size() == 2 && b.droppedFrames() == 2);
    for (int i = 0; i < BLINKER4G_RX_QUEUE_SIZE + 2; ++i) stream.feed("{\"data\":{\"q\":1}}");
    b.pollIncoming();
    assert(b._queueCount == BLINKER4G_RX_QUEUE_SIZE && b.droppedFrames() == 4);
    Blinker4G soft(4, 5);
    soft.begin("key");
    assert(soft.receiveEnabled());
    soft._softwareSerial->overflowed = true;
    soft.pollIncoming();
    assert(soft.serialOverflows() == 1 && soft.droppedFrames() == 1);
    Blinker4G uart(Serial);
    Blinker4G::setLogStream(&Serial);
    uart.begin("key");
    assert(Blinker4GLog::output() == nullptr);
    std::cout << "PASS partial timeout, oversized frame recovery, queue bounds, overflow, UART log isolation\n";
}

static void cache() {
    FakeStream stream;
    Blinker4G b(stream);
    ready(b);
    b._config.obtainedAt = 12345;
    assert(b._config.save("key"));
    Blinker4GConfig loaded;
    assert(loaded.load("key") && loaded.obtainedAt == 0);
    assert(!loaded.load("different-key"));
    EEPROM.data[20] ^= 1;
    assert(!loaded.load("key"));
    assert(b._config.save("key"));
    EEPROM.data[2] = EEPROM.data[3] = 255;
    assert(!loaded.load("key"));
    b._config.iotToken = std::string(256, 'x').c_str();
    assert(!b._config.save("key"));
    b._config.iotToken = std::string(240, 'x').c_str();
    b._config.broker = std::string(250, 'x').c_str();
    assert(!b._config.save("key"));
    ready(b);
    EEPROM.commitOk = false;
    assert(!b._config.save("key"));
    EEPROM.commitOk = true;
    std::cout << "PASS cache roundtrip, auth binding, corruption, bounds, commit failure, reboot timestamps\n";
}

static void reconnect() {
    FakeStream stream;
    Blinker4G b(stream);
    ready(b);
    stream.reply = [&](const std::string&) { stream.feed("+MQTTSTA:\"DISCONNECTION\"\r\nOK\r\n"); };
    fakeMillis += 60001;
    b.handleConnected();
    assert(b.isConnected());
    fakeMillis += 3001;
    b.handleConnected();
    assert(b._state == Blinker4G::STATE_DISCONNECTED);
    b.handleDisconnected();
    assert(b._state == Blinker4G::STATE_TRY_CONNECT_MQTT && b._config.valid);
    for (auto origin : {Blinker4G::STATE_TRY_CONNECT_MQTT, Blinker4G::STATE_CONFIGURE_MQTT}) {
        b._state = origin;
        b.changeState(Blinker4G::STATE_WAIT_MQTT_CONNECT);
        fakeMillis += 40000;
        b._lastCredsCheck = 0;
        b.handleWaitMqttConnect();
        assert(b._state == Blinker4G::STATE_CONFIGURE_HTTP);
    }
    stream.reply = [&](const std::string&) { stream.feed("ERROR\r\n"); };
    b.handleConfigureMqtt();
    assert(b._state == Blinker4G::STATE_CONFIGURE_HTTP);
    // Retry deadlines must work through millis() wraparound.
    fakeMillis = UINT32_MAX - 100;
    b._state = Blinker4G::STATE_DISCONNECTED;
    b._config.valid = true;
    b.retryLater(300);
    fakeMillis += 200;
    b.processStateMachine();
    assert(b._state == Blinker4G::STATE_DISCONNECTED);
    fakeMillis += 101;
    b.processStateMachine();
    assert(b._state == Blinker4G::STATE_TRY_CONNECT_MQTT);
    std::cout << "PASS disconnect confirmation, credential reuse, both HTTP fallback paths, timer wraparound\n";
}

static void lifecycle() {
    FakeStream stream;
    Blinker4G b(stream);
    std::string mode = "NET";
    unsigned authRequests = 0;
    stream.reply = [&](const std::string& cmd) {
        if (cmd.find("AT+WKMOD=HTTPD") != std::string::npos) mode = "HTTPD";
        if (cmd.find("AT+WKMOD=MQTT") != std::string::npos) mode = "MQTT";
        if (cmd.find("authKey=") == 0) {
            assert(mode == "HTTPD");
            ++authRequests;
            stream.feed("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n"
                        "{\"detail\":{\"iotId\":\"iot\",\"iotToken\":\"token\",\"uuid\":\"uuid\",");
            stream.later(20, "\"deviceName\":\"name\",\"host\":\"mqtt://broker.example:1883/\",\"port\":1883}}");
        } else if (cmd.find("AT+MQTTSTA") != std::string::npos) {
            stream.feed("+MQTTSTA:\"CONNECTION\"\r\nOK\r\n");
        } else if (cmd.find("AT+") != std::string::npos) stream.feed("\r\nOK\r\n");
    };
    Blinker4GConfig::clear();
    b.begin("key");
    for (int i = 0; i < 30 && !b.isConnected(); ++i) { b.run(); fakeMillis += 1000; }
    assert(b.isConnected() && authRequests == 1);
    assert(b._config.broker == "broker.example");
    b.changeState(Blinker4G::STATE_DISCONNECTED);
    for (int i = 0; i < 30 && !b.isConnected(); ++i) { b.run(); fakeMillis += 1000; }
    assert(b.isConnected() && authRequests == 1);
    b.clearCredentials();
    for (int i = 0; i < 30 && !b.isConnected(); ++i) { b.run(); fakeMillis += 1000; }
    assert(b.isConnected() && authRequests == 2);
    std::cout << "PASS initial HTTP auth -> MQTT -> cached reconnect -> explicit credential refresh\n";
}

int main() {
    Blinker4G::setLogStream(nullptr);
    receiveAndAt();
    recoveryAndBounds();
    cache();
    reconnect();
    lifecycle();
    std::cout << "All regression scenarios passed.\n";
}
