// Stub SD.h for native tests
#pragma once

class File {
public:
    operator bool() const { return false; }
    void close() {}
};
