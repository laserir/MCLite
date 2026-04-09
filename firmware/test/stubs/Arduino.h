// Minimal Arduino stub for native unit tests.
// Provides just enough of the Arduino API to compile util/ and config/ headers.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cctype>
#include <string>
#include <algorithm>

// ArduinoJson: enable Arduino String support, disable everything else
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#define ARDUINOJSON_ENABLE_ARDUINO_STREAM 0
#define ARDUINOJSON_ENABLE_ARDUINO_PRINT 0
#define ARDUINOJSON_ENABLE_PROGMEM 0

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Arduino constrain macro
#define constrain(amt,low,high) ((amt)<(low)?(low):((amt)>(high)?(high):(amt)))

// --- Controllable millis() for tests ---
static uint32_t _stub_millis_value = 0;
inline void     stub_set_millis(uint32_t ms) { _stub_millis_value = ms; }
inline uint32_t millis() { return _stub_millis_value; }

// --- Minimal String class (mirrors Arduino String API subset) ---
class String {
public:
    String() = default;
    String(const char* s) : _str(s ? s : "") {}
    String(const char* s, size_t len) : _str(s, len) {}
    String(int val)   : _str(std::to_string(val)) {}
    String(unsigned int val) : _str(std::to_string(val)) {}
    String(long val)  : _str(std::to_string(val)) {}
    String(unsigned long val) : _str(std::to_string(val)) {}
    String(float val) { char buf[32]; snprintf(buf, sizeof(buf), "%.2f", val); _str = buf; }

    // Assignment from null pointer (ArduinoJson uses this to clear)
    String& operator=(const char* s) { _str = s ? s : ""; return *this; }

    size_t length() const { return _str.length(); }
    const char* c_str() const { return _str.c_str(); }
    char operator[](size_t i) const { return _str[i]; }

    bool isEmpty() const { return _str.empty(); }

    String& operator+=(char c)            { _str += c; return *this; }
    String& operator+=(const char* s)     { _str += s; return *this; }
    String& operator+=(const String& s)   { _str += s._str; return *this; }

    friend String operator+(const String& a, const String& b) {
        String r; r._str = a._str + b._str; return r;
    }
    friend String operator+(const char* a, const String& b) {
        String r; r._str = std::string(a) + b._str; return r;
    }
    friend String operator+(const String& a, const char* b) {
        String r; r._str = a._str + std::string(b); return r;
    }

    bool operator==(const String& o) const { return _str == o._str; }
    bool operator==(const char* o)   const { return _str == o; }
    bool operator!=(const String& o) const { return _str != o._str; }
    bool operator!=(const char* o)   const { return _str != o; }

    int indexOf(const char* s) const {
        auto pos = _str.find(s);
        return pos == std::string::npos ? -1 : (int)pos;
    }
    bool startsWith(const char* prefix) const {
        return _str.rfind(prefix, 0) == 0;
    }

    String substring(size_t from) const {
        if (from >= _str.length()) return String();
        return String(_str.substr(from).c_str());
    }
    String substring(size_t from, size_t to) const {
        if (from >= _str.length()) return String();
        return String(_str.substr(from, to - from).c_str());
    }

    // ArduinoJson Writer needs concat()
    bool concat(const char* s) {
        if (s) _str += s;
        return true;
    }
    bool concat(char c) {
        _str += c;
        return true;
    }

private:
    std::string _str;
};

// --- Stub Serial ---
struct SerialStub {
    template<typename... Args>
    void printf(const char*, Args...) {}
    void println(const char*) {}
    void print(const char*) {}
};
static SerialStub Serial;
