# Blinker4G

通过 WH-LTE-7S0 4G 模组接入 Blinker 的 Arduino 库。HTTP 认证和 MQTT 连接由模组执行，ESP8266 负责串口通信、JSON 解析和用户逻辑。

## 环境与功能

- 优先支持 ESP8266；保留 ESP32 接口兼容性，ESP32 尚未进行本轮编译或实机验证。
- ArduinoJson 6（>= 6.18.0、< 7.0.0），EspSoftwareSerial >= 8.0.0。ESP8266 Arduino Core 3.1.2 自带的 EspSoftwareSerial 8.0.1 可用。
- HTTP 获取凭证、带完整性校验和 authKey 指纹的 EEPROM 缓存。
- 双向 MQTT 数据、状态/连接/断开回调、断线后优先复用凭证重连。
- AT 应答与 JSON 业务消息分流，支持分片、连续 JSON、字符串内的括号和转义字符。

## ESP8266 软串口快速使用

模组 TX 接 ESP8266 GPIO4，模组 RX 接 GPIO5，两者共地。GPIO 数字不是开发板上的 D 编号。

**先把模组串口设置为 9600、8N1。手册的出厂波特率是 115200；构造函数中的 9600 只配置 ESP8266，不会修改模组。** 可通过厂商工具配置，或按模组当前波特率发送带命令密码的 `AT+UART=9600,8,1,NONE,NONE`，再按手册保存配置并重启。确认双方波特率一致后运行示例。

```cpp
#include <Blinker4G.h>

Blinker4G blinker(4, 5, 9600, "usr.cn#");

void setup() {
    Serial.begin(115200);
    Blinker4G::setLogStream(&Serial);
    blinker.onData([](const char* data) {
        Serial.printf("[RX] %s\n", data);
        blinker.sendData("\"ack\":true");
    });
    blinker.begin("your-auth-key");
}

void loop() {
    blinker.run();
}
```

完整按钮、LED 和传感器上报示例：[BasicUsage](examples/BasicUsage/BasicUsage.ino)。

`sendData()` 接收 **JSON 对象内部的字段片段**，例如 `"\"temp\":25,\"led\":\"on\""`，不要再包最外层 `{}`。`onData()` 收到的是完整的 `data` 对象 JSON，例如 `{"led":"on"}`；回调参数指针只在回调期间有效，需要保存时复制为 `String`。

默认 AUTO 策略现在在 ESP8266 软串口上也开启接收。`FORCE_OFF` 仅用于明确需要发送专用模式的项目，仍会读取 AT 应答。软串口的可靠性仍取决于中断负载、波特率和主循环的响应速度。

## 推荐：ESP8266 硬串口双向通信

使用 [ESP8266HardwareSerial](examples/ESP8266HardwareSerial/ESP8266HardwareSerial.ino)。它将 UART0 切换到 GPIO13/GPIO15，模块通信与调试日志分离：

| 信号 | ESP8266 引脚 |
| --- | --- |
| 模组 TX -> MCU RX | GPIO13，UART0 swap 后的 RX |
| MCU TX -> 模组 RX | GPIO15，UART0 swap 后的 TX |
| 日志 TX -> 独立 USB 串口适配器 RX | GPIO2，Serial1 TX |
| GND | 模组、ESP8266、日志适配器共地 |

GPIO15 是启动配置引脚，复位时必须保持开发板要求的低电平，模组连接不能破坏启动电平。GPIO2 同样需要保留开发板要求的启动电平。使用 swap 后 UART0 的运行数据不会出现在开发板原 USB 串口监视器中；日志需从 GPIO2 接独立适配器读取。

**ESP8266 的 `Serial1` 仅能发送，不能用来连接需要双向通信的模组。** 它适合作为日志输出：

```cpp
Blinker4G blinker(Serial, "usr.cn#");

void setup() {
    Serial1.begin(115200);
    Blinker4G::setLogStream(&Serial1);
    Serial.setRxBufferSize(1024);
    Serial.begin(9600);
    Serial.swap();
    blinker.begin("your-auth-key");
}
```

传入外部 `Stream` 时，由调用者完成串口初始化。库日志默认输出到 `Serial`；若 `begin()` 发现日志和模组共用同一个串口对象，会关闭库日志以避免污染通信。用户自己的 `Serial.print()` 也必须迁移到日志串口。调用 `Blinker4G::setLogStream(nullptr)` 可关闭库日志；日志输出设置由所有实例共享。

## 接收、发送与重连行为

- `run()` 每轮仅主动让出 1ms，循环前后都处理接收。AT 等待、启动等待和发送过程中也会读取串口；业务回调在 AT 事务结束且进入 CONNECTED 后执行，可在回调里调用 `sendData()`。
- 接收消息队列有界。默认最大 JSON 帧 1024 字节，队列 4 帧；队列满时丢弃新帧。半包在最后一个字节之后 2 秒未完成会丢弃；超长帧丢弃至其结束，随后恢复组帧。
- `droppedFrames()` 返回检测到的组帧超时、超长、队列溢出和软串口溢出事件累计数；不是网络端的精确丢包统计。`serialOverflows()` 记录库内部软串口报告的溢出次数；外部 Stream 的底层溢出不能通过此接口检测。
- 模组按串口打包规则形成 MQTT 消息，换行不是可靠的包边界。连续发送之间默认保留 60ms 空闲，适配模组默认 `UARTFT=50ms`；若修改了 UARTFT，必须把库的 TX_GAP 设置得更大。
- 在线时每 60 秒查询 MQTT 状态；首次失败约 3 秒后复查，连续两次失败触发断开回调。先复用现有凭证配置 MQTT；连接等待超时后，先配置并重启到 HTTP 模式，再重新认证和配置 MQTT。
- 不使用保存的 `millis()` 推算跨重启的凭证有效期，也不假设凭证固定一小时到期。`clearCredentials()` 会清空缓存，并在后续 `run()` 中切回 HTTP 重新认证；`restartModule()` 和 `factoryReset()` 会重新进入初始化流程，后者同时清除本地凭证。
- 认证和配置阶段仍含同步 AT 事务及启动等待，单次 `run()` 可能持续数秒；本版本不是完全非阻塞状态机。正常通信时也请避免长时间阻塞主循环，不要在回调里递归调用 `run()`。

## 配置与缓存升级

配置入口：[Blinker4GModuleConfig.h](src/Blinker4GModuleConfig.h)。可直接修改默认值，或通过项目全局编译参数 `-D...` 覆盖。只在 `.ino` 中定义宏不能改变库 `.cpp` 的编译配置。

| 宏 | 默认值 | 含义 |
| --- | --- | --- |
| `BLINKER4G_RX_POLICY` | `BLINKER4G_RX_POLICY_AUTO` | AUTO / FORCE_ON 开启接收，FORCE_OFF 关闭业务下行 |
| `BLINKER4G_SOFT_RX_SIZE` | 256 | 内部软串口字节缓冲 |
| `BLINKER4G_SOFT_ISR_SIZE` | 1024 | 内部软串口边沿缓冲项数，每项约 4 字节 |
| `BLINKER4G_RX_FRAME_SIZE` | 1024 | 最大 JSON 帧字节数，解析文档容量为其两倍 |
| `BLINKER4G_RX_QUEUE_SIZE` | 4 | 等待派发的最大消息数 |
| `BLINKER4G_RX_TIMEOUT_MS` | 2000 | 半包无新字节的超时 |
| `BLINKER4G_TX_GAP_MS` | 60 | 两次串口发送之间的空闲时间 |
| `BLINKER4G_ENABLE_DEBUG` | 0 | 1 开启额外调试；AT 日志不打印含密码的原始命令/应答 |

EEPROM 使用前 512 字节。新格式包含版本、总长度、完整性校验和 authKey 指纹；旧格式会自动失效，升级后首次启动会重新认证。字段超过 255 字节或总空间不足时不会写入，保存失败时继续使用 RAM 中的凭证。authKey 指纹用于区分设备配置，不是加密措施。

## 验证与实机排查

本轮在 ESP8266 Arduino Core 3.1.2、EspSoftwareSerial 8.0.1、ArduinoJson 6.18.0 下编译两个示例；协议回归测试使用实际库代码与模拟串口/时钟/EEPROM。尚未对接真实模组完成长时间运行验证。

主机回归测试需要 Python、支持 C++11 的 g++ 和 ArduinoJson 6 源码：

```sh
python tests/run_native.py --arduinojson /path/to/ArduinoJson
```

实机建议先运行硬串口示例：从 App 连续下发带 `seq` 编号的数据，检查串口输出和回传是否对应；断开并恢复模组网络，观察 DISCONNECTED、TRY_MQTT、CONNECTED 状态及恢复后的收发。再用软串口示例对照，记录 `droppedFrames()`、`serialOverflows()` 和空闲堆。若只有软串口出现溢出，优先检查中断负载和接收调度；若两者都失败，核对原始模组应答、模式和网络状态。
