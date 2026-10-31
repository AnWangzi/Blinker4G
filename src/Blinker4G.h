#ifndef BLINKER4G_H
#define BLINKER4G_H

#include <Arduino.h>
#include <SoftwareSerial.h>
#include <ArduinoJson.h>
#include "Blinker4GLog.h"
#include "Blinker4GModuleDriver.h"
#include "Blinker4GConfig.h"

class Blinker4G {
public:
    Blinker4G(uint8_t rxPin, uint8_t txPin, uint32_t baud = 9600,
              const char* cmdPassword = "usr.cn#");
    Blinker4G(Stream& serial, const char* cmdPassword = "usr.cn#");
    ~Blinker4G() { delete _softwareSerial; }
    Blinker4G(const Blinker4G&) = delete;
    Blinker4G& operator=(const Blinker4G&) = delete;
    
    void begin(const char* authKey);
    void run();
    static void setLogStream(Print* stream) { Blinker4GLog::output() = stream; }
    unsigned long droppedFrames() const { return _droppedFrames; }
    unsigned long serialOverflows() const { return _serialOverflows; }
    
    void onData(void (*callback)(const char* data));
    void onState(void (*callback)(const char* state));
    void onConnect(void (*callback)());
    void onDisconnect(void (*callback)());
    
    void sendData(const char* data);
    void sendState(const char* state);
    
    bool isConnected();
    const char* getDeviceId();
    const char* getDeviceName();
    const char* getIotId();
    
    void restartModule();
    void factoryReset();
    void clearCredentials();

private:
    friend class Blinker4GWhLte7S0Driver;

    uint8_t _rxPin, _txPin;
    uint32_t _baud;
    const char* _cmdPassword;
    String _authKey;
    
    Stream* _moduleSerial;
    SoftwareSerial* _softwareSerial;
    bool _ownsSerial;
    Blinker4GConfig _config;
    Blinker4GModuleDriver _driver;
    
    enum State {
        STATE_INIT,
        STATE_LOAD_EEPROM,
        STATE_CONFIGURE_HTTP,
        STATE_GET_CREDENTIALS,
        STATE_TRY_CONNECT_MQTT,
        STATE_CONFIGURE_MQTT,
        STATE_WAIT_MQTT_CONNECT,
        STATE_CONNECTED,
        STATE_DISCONNECTED
    };
    State _state;
    State _prevState;
    unsigned long _stateEnterTime;
    
    unsigned long _lastHeartbeat;
    unsigned long _lastCredsCheck;
    unsigned long _retryAt = 0;
    bool _retryPending = false;
    uint8_t _statusFailures = 0;
    String _recvBuffer;
    int _braceDepth = 0;
    bool _inString = false;
    bool _escaped = false;
    bool _discardFrame = false;
    unsigned long _lastRxAt = 0;
    String _pending[BLINKER4G_RX_QUEUE_SIZE];
    uint8_t _queueHead = 0, _queueCount = 0;
    unsigned long _droppedFrames = 0, _serialOverflows = 0;
    String _atLine, _atResponse, _httpBody;
    bool _atActive = false, _atDone = false, _captureHttp = false;
    bool _lineOverflow = false;
    bool _hasWritten = false;
    unsigned long _lastWriteAt = 0;
    
    void (*_dataCallback)(const char* data) = nullptr;
    void (*_stateCallback)(const char* state) = nullptr;
    void (*_connectCallback)() = nullptr;
    void (*_disconnectCallback)() = nullptr;
    
    void changeState(State newState);
    String sendAT(const char* cmd, unsigned long timeoutMs);
    
    bool initModule();
    bool configureHttpBase();
    bool getCredentials();
    bool configureMqtt();
    
    void publishData(const char* payload);
    bool receiveEnabled() const;
    void handleIncoming(const char* rawData);
    void pollIncoming();
    void resetFrame();
    void dispatchIncoming();
    void waitWithReceive(unsigned long duration);
    void writeFrame(const String& frame);
    void retryLater(unsigned long duration);
    void processStateMachine();
    
    void handleInit();
    void handleLoadEeprom();
    void handleConfigureHttp();
    void handleGetCredentials();
    void handleTryConnectMqtt();
    void handleConfigureMqtt();
    void handleWaitMqttConnect();
    void handleConnected();
    void handleDisconnected();
};

#endif
