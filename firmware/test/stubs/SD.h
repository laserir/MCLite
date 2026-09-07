// Stub SD.h for native tests
#pragma once

class File {
public:
    operator bool() const { return false; }
    void close() {}
    // Enough of the Arduino File surface for I18n::init()'s lang-directory scan
    // to compile on the host. Always falsey, so the scan no-ops.
    bool isDirectory() const { return false; }
    File openNextFile() { return File(); }
    const char* name() const { return ""; }
    size_t size() const { return 0; }
};
