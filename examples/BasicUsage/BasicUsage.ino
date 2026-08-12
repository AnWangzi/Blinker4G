#include <Blinker4G.h>

#define MODULE_RX 4
#define MODULE_TX 5
#define BLINKER_AUTH_KEY "your-auth-key"
#define MODULE_CMD_PASSWORD "usr.cn#"
#define REPORT_INTERVAL_MS 15000UL

Blinker4G blinker(MODULE_RX, MODULE_TX, 9600, MODULE_CMD_PASSWORD);

float temperature = 25.0;
bool ledState = false;
unsigned long lastReportAt = 0;

void applyLedState(bool enabled) {
    ledState = enabled;
    digitalWrite(LED_BUILTIN, ledState ? LOW : HIGH);
}

void handleTapEvent(const char* key) {
    applyLedState(!ledState);

    char data[128];
    snprintf(data, sizeof(data), "\"led\":\"%s\",\"lastButton\":\"%s\"",
             ledState ? "on" : "off", key);
    blinker.sendData(data);

    Serial.printf("[Callback] Handled tap event from %s\n", key);
}

void onDataReceived(const char* data) {
    Serial.printf("[Callback] Data: %s\n", data);

    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, data)) {
        Serial.println("[Callback] Invalid JSON payload");
        return;
    }

    for (JsonPair kv : doc.as<JsonObject>()) {
        const char* key = kv.key().c_str();
        const char* value = kv.value().as<const char*>();

        if (strncmp(key, "btn-", 4) == 0 && value && strcmp(value, "tap") == 0) {
            handleTapEvent(key);
            return;
        }
    }

    const char* ledCommand = doc["led"];
    if (ledCommand) {
        applyLedState(strcmp(ledCommand, "on") == 0);
    }
}

void reportSensorData() {
    temperature += (random(-20, 21) / 10.0f);
    if (temperature > 40.0f || temperature < 10.0f) {
        temperature = 25.0f;
    }

    char data[128];
    snprintf(data, sizeof(data), "\"temp\":%.1f,\"led\":\"%s\"",
             temperature, ledState ? "on" : "off");
    blinker.sendData(data);

    Serial.printf("[Report] %s\n", data);
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
    applyLedState(false);

    Serial.println();
    Serial.println("[Example] Blinker4G BasicUsage starting");
    Serial.println("[Example] Update BLINKER_AUTH_KEY and MODULE_CMD_PASSWORD before flashing");

    blinker.onData(onDataReceived);
    blinker.onState([](const char* state) {
        Serial.printf("[State] %s\n", state);
    });
    blinker.onConnect([]() {
        Serial.println("[Connect] Connected to Blinker");
        Serial.printf("[Connect] Device ID: %s\n", blinker.getDeviceId());
        Serial.printf("[Connect] Device Name: %s\n", blinker.getDeviceName());
        Serial.printf("[Connect] IoT ID: %s\n", blinker.getIotId());
    });
    blinker.onDisconnect([]() {
        Serial.println("[Connect] Disconnected from Blinker");
    });

    blinker.begin(BLINKER_AUTH_KEY);
}

void loop() {
    blinker.run();

    if (blinker.isConnected() && millis() - lastReportAt >= REPORT_INTERVAL_MS) {
        lastReportAt = millis();
        reportSensorData();
    }
}