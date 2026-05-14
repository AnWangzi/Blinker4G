# Blinker4G
<p align="center">
  <img src="https://diandeng.tech/assets/img/logo-top.png" width="200">
</p>
基于 WH-LTE-7S0 4G 模块连接 Blinker IoT 平台的 Arduino 库。

## 功能
- 自动获取 MQTT 凭证
- EEPROM 凭证缓存
- 双向数据通信（发布/订阅）
- 自动重连

## 硬件
- ESP8266 / ESP32
- 有人物联网 WH-LTE-7S0

## 使用

#include <Blinker4G.h>

Blinker4G blinker(4, 5, 9600, "你的指令密码");

void setup() {
    Serial.begin(115200);
    blinker.onData([](const char* data) {
        Serial.println(data);
    });
    blinker.begin("你的authKey");
}

void loop() {
    blinker.run();
}