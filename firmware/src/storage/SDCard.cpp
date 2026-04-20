#include "SDCard.h"
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
