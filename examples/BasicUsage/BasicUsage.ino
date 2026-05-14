#include <Blinker4G.h>

#define MODULE_RX 4
#define MODULE_TX 5
#define BLINKER_AUTH_KEY "782ab2eedaa3"

Blinker4G blinker(MODULE_RX, MODULE_TX, 9600, "wz2m.cn#");

float temperature = 25.0;
bool ledState = false;

void onDataReceived(const char* data) {
    Serial.printf("[Callback] Data: %s\n", data);
    
    StaticJsonDocument<256> doc;
    if (!deserializeJson(doc, data)) {
        if (doc.containsKey("btn-d2b")) {
            ledState = !ledState;
            digitalWrite(LED_BUILTIN, ledState);
        }
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
    
    blinker.onData(onDataReceived);
    blinker.onConnect([]() {
        Serial.println("Connected to Blinker!");
    });
    
    blinker.begin(BLINKER_AUTH_KEY);
}

void loop() {
    blinker.run();
    
    static unsigned long lastReport = 0;
    if (millis() - lastReport > 60000 && blinker.isConnected()) {
        lastReport = millis();
        temperature += (random(-20, 21) / 10.0);
        if (temperature > 40) temperature = 25;
        if (temperature < 10) temperature = 25;
        
        char data[128];
        sprintf(data, "\"temp\":%.1f,\"led\":\"%s\"",
                temperature, ledState ? "on" : "off");
        blinker.sendData(data);
    }
}