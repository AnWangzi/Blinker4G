#pragma once
#include <array>
#include <cassert>
#include <cstdint>
struct FakeEEPROM {
    std::array<uint8_t, 512> data{};
    bool commitOk = true;
    void begin(size_t size) { assert(size == data.size()); }
    uint8_t read(size_t addr) { assert(addr < data.size()); return data[addr]; }
    void write(size_t addr, uint8_t value) { assert(addr < data.size()); data[addr] = value; }
    bool commit() { return commitOk; }
    void end() {}
};
extern FakeEEPROM EEPROM;
