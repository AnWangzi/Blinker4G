#pragma once
#include "Arduino.h"
#define SWSERIAL_8N1 0
class SoftwareSerial : public NullSerial {
public:
    SoftwareSerial(uint8_t, uint8_t) {}
    void begin(uint32_t, int, int, int, bool, int, int) {}
    bool overflow() { bool result = overflowed; overflowed = false; return result; }
    bool overflowed = false;
};
