#ifndef BLINKER4G_MODULE_CONFIG_H
#define BLINKER4G_MODULE_CONFIG_H

#define BLINKER4G_MODULE_WH_LTE_7S0 1

#ifndef BLINKER4G_MODULE
#define BLINKER4G_MODULE BLINKER4G_MODULE_WH_LTE_7S0
#endif

#if defined(ESP8266) || defined(ARDUINO_ARCH_ESP8266)
#define BLINKER4G_PLATFORM_ESP8266 1
#else
#define BLINKER4G_PLATFORM_ESP8266 0
#endif

#if defined(ESP32) || defined(ARDUINO_ARCH_ESP32)
#define BLINKER4G_PLATFORM_ESP32 1
#else
#define BLINKER4G_PLATFORM_ESP32 0
#endif

// 0: normal logs, 1: verbose debug logs
#ifndef BLINKER4G_ENABLE_DEBUG
#define BLINKER4G_ENABLE_DEBUG 0
#endif

#define BLINKER4G_RX_POLICY_AUTO 0
#define BLINKER4G_RX_POLICY_FORCE_OFF 1
#define BLINKER4G_RX_POLICY_FORCE_ON 2

#ifndef BLINKER4G_RX_POLICY
#define BLINKER4G_RX_POLICY BLINKER4G_RX_POLICY_AUTO
#endif

// AUTO enables reception on both hardware and software serial.
#ifndef BLINKER4G_RX_FRAME_SIZE
#define BLINKER4G_RX_FRAME_SIZE 1024
#endif
#ifndef BLINKER4G_RX_QUEUE_SIZE
#define BLINKER4G_RX_QUEUE_SIZE 4
#endif
#ifndef BLINKER4G_RX_TIMEOUT_MS
#define BLINKER4G_RX_TIMEOUT_MS 2000UL
#endif
#ifndef BLINKER4G_SOFT_RX_SIZE
#define BLINKER4G_SOFT_RX_SIZE 256
#endif
#ifndef BLINKER4G_SOFT_ISR_SIZE
#define BLINKER4G_SOFT_ISR_SIZE 1024
#endif
// Must exceed the module's UARTFT setting (default 50ms).
#ifndef BLINKER4G_TX_GAP_MS
#define BLINKER4G_TX_GAP_MS 60UL
#endif

static_assert(BLINKER4G_RX_QUEUE_SIZE > 0 && BLINKER4G_RX_QUEUE_SIZE <= 255,
              "RX queue size must be between 1 and 255");

#endif
