#include <Blinker4G.h>

// 4G模块引脚
#define MODULE_RX 4
#define MODULE_TX 5

// Blinker 设备 AuthKey
#define BLINKER_AUTH_KEY "782ab2eedaa3"

// 创建 Blinker4G 实例
Blinker4G blinker(MODULE_RX, MODULE_TX, 9600, "wz2m.cn#");

// 设备数据
float temperature = 25.0;
bool ledState = false;

// 数据回调
void onDataReceived(const char* data) {
    Serial.printf("[Callback] Data: %s\n", data);
    
    // 解析数据
    StaticJsonDocument<256> doc;
    deserializeJson(doc, data);
    
    // 处理按钮
    if (doc.containsKey("btn-d2b")) {
        ledState = !ledState;
        digitalWrite(LED_BUILTIN, ledState);
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);
    
    // 设置回调
    blinker.onData(onDataReceived);
    blinker.onConnect([]() {
        Serial.println("Connected to Blinker!");
    });
    
    // 初始化
    blinker.begin(BLINKER_AUTH_KEY);
}

void loop() {
    blinker.run();
    
    // 每 60 秒上报一次温度
    static unsigned long lastReport = 0;
    if (millis() - lastReport > 60000 && blinker.isConnected()) {
        lastReport = millis();
        
        char data[128];
        sprintf(data, "{\"temp\":%.1f,\"led\":\"%s\"}",
                temperature, ledState ? "on" : "off");
        blinker.sendData(data);
    }
}