#ifndef BLINKER4GCONFIG_H
#define BLINKER4GCONFIG_H

#include <Arduino.h>
#include <EEPROM.h>

// EEPROM 配置
#define BLINKER4G_EEPROM_SIZE        512
#define BLINKER4G_EEPROM_START       0
#define BLINKER4G_EEPROM_MAGIC       0xA5  // 数据有效性标志

struct Blinker4GConfig {
    String broker;
    int port = 1883;
    String iotId;
    String iotToken;
    String deviceName;
    String deviceId;
    bool valid = false;
    unsigned long obtainedAt = 0;
    
    // 保存到 EEPROM
    bool save() {
        EEPROM.begin(BLINKER4G_EEPROM_SIZE);
        
        int addr = BLINKER4G_EEPROM_START;
        
        // 写入魔法数字（标记数据有效）
        EEPROM.write(addr++, BLINKER4G_EEPROM_MAGIC);
        
        // 写入获取时间戳
        EEPROM.put(addr, obtainedAt);
        addr += sizeof(unsigned long);
        
        // 写入各字段
        writeString(addr, broker);
        writeString(addr, iotId);
        writeString(addr, iotToken);
        writeString(addr, deviceName);
        writeString(addr, deviceId);
        
        // 写入端口和有效标志
        EEPROM.put(addr, port);
        
        EEPROM.commit();
        EEPROM.end();
        
        Serial.println(F("[EEPROM] Config saved"));
        return true;
    }
    
    // 从 EEPROM 加载
    bool load() {
        EEPROM.begin(BLINKER4G_EEPROM_SIZE);
        
        int addr = BLINKER4G_EEPROM_START;
        
        // 检查魔法数字
        if (EEPROM.read(addr++) != BLINKER4G_EEPROM_MAGIC) {
            Serial.println(F("[EEPROM] No valid data"));
            EEPROM.end();
            return false;
        }
        
        // 读取时间戳
        EEPROM.get(addr, obtainedAt);
        addr += sizeof(unsigned long);
        
        // 读取各字段
        readString(addr, broker);
        readString(addr, iotId);
        readString(addr, iotToken);
        readString(addr, deviceName);
        readString(addr, deviceId);
        
        // 读取端口
        EEPROM.get(addr, port);
        
        EEPROM.end();
        
        valid = (iotId.length() > 0 && iotToken.length() > 0 && deviceId.length() > 0);
        
        if (valid) {
            Serial.println(F("[EEPROM] Config loaded"));
            Serial.printf("  Device: %s\n", deviceId.c_str());
            Serial.printf("  Broker: %s:%d\n", broker.c_str(), port);
        }
        
        return valid;
    }
    
    // 清除 EEPROM 数据
    static void clear() {
        EEPROM.begin(BLINKER4G_EEPROM_SIZE);
        EEPROM.write(BLINKER4G_EEPROM_START, 0);  // 清除魔法数字
        EEPROM.commit();
        EEPROM.end();
        Serial.println(F("[EEPROM] Config cleared"));
    }
    
private:
    void writeString(int& addr, const String& str) {
        uint8_t len = str.length();
        EEPROM.write(addr++, len);
        for (int i = 0; i < len; i++) {
            EEPROM.write(addr++, str[i]);
        }
    }
    
    void readString(int& addr, String& str) {
        uint8_t len = EEPROM.read(addr++);
        str = "";
        for (int i = 0; i < len; i++) {
            str += (char)EEPROM.read(addr++);
        }
    }
};

#endif