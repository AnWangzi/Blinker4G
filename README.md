# Blinker4G
<p align="center">
  <img src="https://diandeng.tech/assets/img/logo-top.png" width="200">
</p>
基于 WH-LTE-7S0 4G 模块连接 Blinker IoT 平台的 Arduino 库。

## 模块驱动
当前默认使用 WH-LTE-7S0 驱动，驱动选择通过 `src/Blinker4GModuleConfig.h` 里的 `BLINKER4G_MODULE` 宏控制。
后续如果新增其他模块，只需要新增对应驱动文件并切换这个宏即可。

## 功能
- 自动获取 MQTT 凭证
- EEPROM 凭证缓存
- 双向数据通信（发布/订阅）
- 自动重连
- 状态回调
- 分级日志与时间戳

## 日志
库内日志统一输出为 `[时间戳][等级][标签] 消息`。
默认输出普通日志，比如状态变化、连接过程、业务收发和错误信息。
如果打开库级调试开关，还会额外输出更详细的调试日志，比如 AT 指令收发和底层状态细节。

直接改 [src/Blinker4GModuleConfig.h](src/Blinker4GModuleConfig.h) 里的这一行即可：

```cpp
#define BLINKER4G_ENABLE_DEBUG 0
```

要开详细调试日志，把 `0` 改成 `1`。

## 平台策略
库现在会根据编译平台自动选择接收策略：

- `ESP8266 + 库内部 SoftwareSerial`：默认关闭接收，只保留稳定发送
- `ESP8266 + 外部 Stream/硬串口`：默认开启接收
- `ESP32`：默认开启接收

这个自动策略由 [src/Blinker4GModuleConfig.h](src/Blinker4GModuleConfig.h) 里的 `BLINKER4G_RX_POLICY` 控制。
默认值是：

```cpp
#define BLINKER4G_RX_POLICY BLINKER4G_RX_POLICY_AUTO
```

如果你要强制覆盖，可以改成：

```cpp
#define BLINKER4G_RX_POLICY BLINKER4G_RX_POLICY_FORCE_OFF
```

或：

```cpp
#define BLINKER4G_RX_POLICY BLINKER4G_RX_POLICY_FORCE_ON
```

## 硬件
- ESP8266(已验证，发送正常，接收不正常) / ESP32(未验证)
- 有人物联网 WH-LTE-7S0

说明：ESP8266 上 `SoftwareSerial` 长时间跑 MQTT 时可能出现吞字或半包，因此自动策略默认不把它作为可靠接收通道。优先建议使用硬串口或外部 `Stream` 构造函数。

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

如果你已经有可用的硬串口，也可以直接传入 `Stream`：

```cpp
Blinker4G blinker(Serial1, "你的指令密码");
```