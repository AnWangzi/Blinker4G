#ifndef BLINKER4GCONFIG_H
#define BLINKER4GCONFIG_H

#include <Arduino.h>
#include <EEPROM.h>
#include "Blinker4GLog.h"

#define BLINKER4G_EEPROM_SIZE 512
#define BLINKER4G_EEPROM_START 0
#define BLINKER4G_EEPROM_MAGIC 0xA5

struct Blinker4GConfig {
    String broker, iotId, iotToken, deviceName, deviceId;
    int port = 1883;
    bool valid = false;
    // Session uptime only; never persisted across reboots.
    unsigned long obtainedAt = 0;

    bool complete() const {
        return broker.length() && iotId.length() && iotToken.length() &&
               deviceName.length() && deviceId.length() && port > 0 && port <= 65535;
    }

    bool save(const char* authKey = "") {
        if (!complete()) return false;
        uint8_t bytes[BLINKER4G_EEPROM_SIZE] = {};
        size_t pos = 12;
        const String* fields[] = {&broker, &iotId, &iotToken, &deviceName, &deviceId};
        for (auto field : fields) {
            size_t len = field->length();
            if (len > 255 || pos + 1 + len + 2 > sizeof(bytes)) return false;
            bytes[pos++] = (uint8_t)len;
            memcpy(bytes + pos, field->c_str(), len);
            pos += len;
        }
        bytes[pos++] = (uint8_t)port;
        bytes[pos++] = (uint8_t)(port >> 8);
        bytes[0] = BLINKER4G_EEPROM_MAGIC;
        bytes[1] = 2;
        bytes[2] = (uint8_t)pos;
        bytes[3] = (uint8_t)(pos >> 8);
        uint32_t keyHash = checksum((const uint8_t*)authKey, strlen(authKey));
        memcpy(bytes + 8, &keyHash, 4);
        uint32_t sum = checksum(bytes + 8, pos - 8);
        memcpy(bytes + 4, &sum, 4);
        EEPROM.begin(BLINKER4G_EEPROM_SIZE);
        for (size_t i = 0; i < sizeof(bytes); ++i) EEPROM.write(i, bytes[i]);
        bool saved = EEPROM.commit();
        EEPROM.end();
        return saved;
    }

    bool load(const char* authKey = "") {
        valid = false;
        uint8_t bytes[BLINKER4G_EEPROM_SIZE];
        EEPROM.begin(BLINKER4G_EEPROM_SIZE);
        for (size_t i = 0; i < sizeof(bytes); ++i) bytes[i] = EEPROM.read(i);
        EEPROM.end();
        size_t end = bytes[2] | (size_t(bytes[3]) << 8);
        if (bytes[0] != BLINKER4G_EEPROM_MAGIC || bytes[1] != 2 ||
            end < 19 || end > sizeof(bytes)) return false;
        uint32_t sum, keyHash;
        memcpy(&sum, bytes + 4, 4);
        memcpy(&keyHash, bytes + 8, 4);
        if (sum != checksum(bytes + 8, end - 8) ||
            keyHash != checksum((const uint8_t*)authKey, strlen(authKey))) return false;
        Blinker4GConfig candidate;
        String* fields[] = {&candidate.broker, &candidate.iotId, &candidate.iotToken,
                           &candidate.deviceName, &candidate.deviceId};
        size_t pos = 12;
        for (auto field : fields) {
            if (pos >= end - 2) return false;
            size_t len = bytes[pos++];
            if (pos + len > end - 2) return false;
            field->reserve(len);
            for (size_t i = 0; i < len; ++i) *field += (char)bytes[pos++];
        }
        if (pos + 2 != end) return false;
        candidate.port = bytes[pos] | (unsigned(bytes[pos + 1]) << 8);
        candidate.valid = candidate.complete();
        if (!candidate.valid) return false;
        *this = candidate;
        return true;
    }

    static void clear() {
        EEPROM.begin(BLINKER4G_EEPROM_SIZE);
        EEPROM.write(BLINKER4G_EEPROM_START, 0);
        EEPROM.commit();
        EEPROM.end();
    }

private:
    // Integrity/fingerprint check only, not encryption or authentication.
    static uint32_t checksum(const uint8_t* data, size_t length) {
        uint32_t value = 2166136261UL;
        for (size_t i = 0; i < length; ++i) value = (value ^ data[i]) * 16777619UL;
        return value;
    }
};
#endif
