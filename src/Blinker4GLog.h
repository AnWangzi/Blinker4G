#ifndef BLINKER4G_LOG_H
#define BLINKER4G_LOG_H

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include "Blinker4GModuleConfig.h"

namespace Blinker4GLog {

inline Print*& output() {
    static Print* stream = &Serial;
    return stream;
}

enum Level {
    LOG_LEVEL_ERROR,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG
};

inline const char* levelName(Level level) {
    switch (level) {
        case LOG_LEVEL_ERROR: return "ERROR";
        case LOG_LEVEL_INFO: return "INFO";
        case LOG_LEVEL_DEBUG: return "DEBUG";
        default: return "INFO";
    }
}

inline bool shouldLog(Level level) {
#if BLINKER4G_ENABLE_DEBUG
    (void)level;
    return true;
#else
    return level != LOG_LEVEL_DEBUG;
#endif
}

inline void vlogf(Level level, const char* tag, const char* format, va_list args) {
    if (!shouldLog(level) || !output()) return;

    char message[256];
    vsnprintf(message, sizeof(message), format, args);
    char prefix[64];
    snprintf(prefix, sizeof(prefix), "[%10lu][%s][%s] ", millis(), levelName(level), tag);
    output()->print(prefix);
    output()->println(message);
}

inline void logf(Level level, const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vlogf(level, tag, format, args);
    va_end(args);
}

}  // namespace Blinker4GLog

#define BLINKER4G_LOG_ERROR(tag, format, ...) Blinker4GLog::logf(Blinker4GLog::LOG_LEVEL_ERROR, tag, format, ##__VA_ARGS__)
#define BLINKER4G_LOG_INFO(tag, format, ...) Blinker4GLog::logf(Blinker4GLog::LOG_LEVEL_INFO, tag, format, ##__VA_ARGS__)
#define BLINKER4G_LOG_DEBUG(tag, format, ...) Blinker4GLog::logf(Blinker4GLog::LOG_LEVEL_DEBUG, tag, format, ##__VA_ARGS__)

#endif
