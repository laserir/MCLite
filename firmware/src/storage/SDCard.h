#pragma once

#include <SD.h>
#include <FS.h>

namespace mclite {

class SDCard {
public:
    bool init();
    bool isMounted() const { return _mounted; }

    bool fileExists(const char* path);
    bool dirExists(const char* path);
    String readFile(const char* path, size_t maxSize = 32768);
    bool writeFile(const char* path, const String& content);
    bool appendFile(const char* path, const String& content);
    bool mkdir(const char* path);
    bool remove(const char* path);

    // Open a file for raw streaming (binary reads). Returns an invalid File
    // when SD not mounted or path missing. Caller must close().
    File openRaw(const char* path);

    static SDCard& instance();

private:
    SDCard() = default;
    bool _mounted = false;
};

}  // namespace mclite
