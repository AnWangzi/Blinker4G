#include <Blinker4G.h>

#if !defined(ESP8266)
#error "Select an ESP8266 board for this example"
#endif

// UART0 after swap: module TX -> GPIO13 (RX), module RX -> GPIO15 (TX).
// GPIO15 is a boot strap pin: keep it LOW during reset, per your board design.
// Logs: GPIO2 (Serial1 TX) -> a separate USB-UART adapter's RX, common GND.
// Serial1 is transmit-only on ESP8266; it is used ONLY for logs.
Blinker4G blinker(Serial, "usr.cn#");
unsigned long lastReportAt = 0;
unsigned long sequence = 0;

void setup() {
    Serial1.begin(115200);
    Blinker4G::setLogStream(&Serial1);
    Serial.setRxBufferSize(1024);
    Serial.begin(9600);  // Must match the module's configured baud rate.
    Serial.swap();

    blinker.onData([](const char* data) {
        Serial1.printf("[RX] %s\n", data);
        // Echo the data object back as an acknowledgement.
        // The data pointer is valid only during this callback.
        String fields(data);
        if (fields.length() >= 2) {
            fields = fields.substring(1, fields.length() - 1);
            blinker.sendData(fields.c_str());
        }
    });
    blinker.onConnect([]() { Serial1.println("[MQTT] connected"); });
    blinker.onDisconnect([]() { Serial1.println("[MQTT] disconnected; retrying"); });
    blinker.begin("your-auth-key");
}

void loop() {
    blinker.run();
    if (blinker.isConnected() && millis() - lastReportAt >= 15000UL) {
        lastReportAt = millis();
        char fields[64];
        snprintf(fields, sizeof(fields), "\"seq\":%lu,\"heap\":%u",
                 ++sequence, ESP.getFreeHeap());
        blinker.sendData(fields);
        Serial1.printf("[RX] dropped=%lu overflow=%lu\n",
                       blinker.droppedFrames(), blinker.serialOverflows());
    }
}
