#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <cstdio>

// Minimal host adapter; protocol/JSON/state-machine code is the real library.
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#define ARDUINOJSON_ENABLE_ARDUINO_STREAM 0
#define ARDUINOJSON_ENABLE_ARDUINO_PRINT 0
#define ARDUINOJSON_ENABLE_PROGMEM 0

class String {
    std::string value;
public:
    String() = default;
    String(const char* s) : value(s ? s : "") {}
    String(const std::string& s) : value(s) {}
    size_t length() const { return value.size(); }
    const char* c_str() const { return value.c_str(); }
    bool reserve(size_t n) { value.reserve(n); return true; }
    char operator[](size_t i) const { return value[i]; }
    String& operator+=(char c) { value += c; return *this; }
    String& operator+=(const char* s) { value += s; return *this; }
    String& operator+=(const String& s) { value += s.value; return *this; }
    friend String operator+(String a, const String& b) { a += b; return a; }
    bool operator==(const char* s) const { return value == s; }
    int indexOf(char c, size_t start = 0) const {
        auto pos = value.find(c, start); return pos == value.npos ? -1 : int(pos);
    }
    int indexOf(const char* s) const {
        auto pos = value.find(s); return pos == value.npos ? -1 : int(pos);
    }
    String substring(size_t start, size_t end = std::string::npos) const {
        return value.substr(start, end == std::string::npos ? end : end - start);
    }
    bool startsWith(const char* s) const { return value.find(s) == 0; }
    bool endsWith(const char* s) const {
        size_t len = strlen(s);
        return value.size() >= len && value.compare(value.size()-len, len, s) == 0;
    }
    void trim() {
        auto start = value.find_first_not_of(" \t\r\n");
        if (start == value.npos) { value.clear(); return; }
        value = value.substr(start, value.find_last_not_of(" \t\r\n") - start + 1);
    }
};

class StringSumHelper : public String {};

class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t) = 0;
    size_t print(const char* s) { size_t n = 0; while (*s) n += write(*s++); return n; }
    size_t println(const char* s) { return print(s) + print("\r\n"); }
};
class Stream : public Print {
public:
    virtual int available() = 0;
    virtual int read() = 0;
    virtual void flush() {}
};
class NullSerial : public Stream {
public:
    size_t write(uint8_t) override { return 1; }
    int available() override { return 0; }
    int read() override { return -1; }
};
extern NullSerial Serial;
extern uint32_t fakeMillis;
inline unsigned long millis() { return fakeMillis; }
inline void delay(unsigned long ms) { fakeMillis += ms; }
inline void yield() {}
