#include "SDCard.h"
#include "hal/boards/board.h"
#include <SPI.h>
#include <Arduino.h>

namespace mclite {

SDCard& SDCard::instance() {
    static SDCard inst;
    return inst;
}

bool SDCard::init() {
    // Deassert LoRa CS and TFT CS to prevent bus contention on shared SPI
    pinMode(TDECK_LORA_CS, OUTPUT);
    digitalWrite(TDECK_LORA_CS, HIGH);
    pinMode(TDECK_TFT_CS, OUTPUT);
    digitalWrite(TDECK_TFT_CS, HIGH);

    // Init Arduino SPI on the same pins LovyanGFX uses (shared bus)
    SPI.begin(TDECK_SPI_SCK, TDECK_SPI_MISO, TDECK_SPI_MOSI, TDECK_SD_CS);
    // 25MHz SPI, max 1 retry (reduces timeout when no card inserted)
    _mounted = SD.begin(TDECK_SD_CS, SPI, 25000000, "/sd", 1);
    if (_mounted) {
        Serial.printf("[SD] Mounted, size: %lluMB\n", SD.totalBytes() / (1024 * 1024));
    } else {
        Serial.println("[SD] Mount failed");
    }
    return _mounted;
}

bool SDCard::fileExists(const char* path) {
    if (!_mounted) return false;
    return SD.exists(path);
}

bool SDCard::dirExists(const char* path) {
    if (!_mounted) return false;
    File f = SD.open(path);
    if (!f) return false;
    bool isDir = f.isDirectory();
    f.close();
    return isDir;
}

File SDCard::openRaw(const char* path) {
    if (!_mounted) return File();
    return SD.open(path, FILE_READ);
}

String SDCard::readFile(const char* path, size_t maxSize) {
    if (!_mounted) return "";
    File f = SD.open(path, FILE_READ);
    if (!f) return "";
    if (f.size() > maxSize) {
        Serial.printf("[SD] File too large: %s (%u bytes, max %u)\n", path, (unsigned)f.size(), (unsigned)maxSize);
        f.close();
        return "";
    }
    String content = f.readString();
    f.close();
    return content;
}

bool SDCard::writeFile(const char* path, const String& content) {
    if (!_mounted) return false;
    File f = SD.open(path, FILE_WRITE);
    if (!f) return false;
    size_t written = f.print(content);
    f.close();
    return written == content.length();
}

bool SDCard::writeAtomic(const char* path, const String& content) {
    if (!_mounted) return false;

    String tmp = String(path) + ".tmp";
    String bak = String(path) + ".bak";

    // Stage to tmp. Truncates if a stale tmp from a prior failed write exists.
    File f = SD.open(tmp.c_str(), FILE_WRITE);
    if (!f) {
        Serial.printf("[SD] writeAtomic: cannot open %s\n", tmp.c_str());
        return false;
    }
    size_t written = f.print(content);
    f.close();
    if (written != content.length()) {
        Serial.printf("[SD] writeAtomic: short write %u/%u\n",
                      (unsigned)written, (unsigned)content.length());
        SD.remove(tmp.c_str());
        return false;
    }

    // If a previous file exists, rotate it to .bak (replacing any older bak).
    if (SD.exists(path)) {
        if (SD.exists(bak.c_str())) SD.remove(bak.c_str());
        if (!SD.rename(path, bak.c_str())) {
            Serial.printf("[SD] writeAtomic: rename %s -> %s failed\n",
                          path, bak.c_str());
            SD.remove(tmp.c_str());
            return false;
        }
    }

    // Promote tmp to the live name.
    if (!SD.rename(tmp.c_str(), path)) {
        Serial.printf("[SD] writeAtomic: rename %s -> %s failed\n",
                      tmp.c_str(), path);
        // bak still exists from the previous step (or never existed) — leave
        // the world recoverable. boot fallback in ConfigManager will pick up
        // the bak.
        SD.remove(tmp.c_str());
        return false;
    }

    // Drop the now-redundant .bak. It was a transactional artifact: kept
    // only long enough to survive a power loss between the two renames
    // above. Steady state is just <path> with no shadow file. If this
    // remove fails (rare), the stale .bak is harmless — the next save's
    // step 2 will clean it before rotating again.
    SD.remove(bak.c_str());
    return true;
}

bool SDCard::appendFile(const char* path, const String& content) {
    if (!_mounted) return false;
    File f = SD.open(path, FILE_APPEND);
    if (!f) return false;
    size_t written = f.print(content);
    f.close();
    return written == content.length();
}

bool SDCard::mkdir(const char* path) {
    if (!_mounted) return false;
    return SD.mkdir(path);
}

bool SDCard::remove(const char* path) {
    if (!_mounted) return false;
    return SD.remove(path);
}

}  // namespace mclite
