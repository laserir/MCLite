#include "FirmwareUpdater.h"
#include "util/log.h"

#include <SD.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <mbedtls/sha256.h>

#include "../storage/SDCard.h"
#include "../config/defaults.h"
#include "../util/version.h"

namespace mclite {

// arduino-esp32 embeds a Mozilla root-CA bundle; this symbol points at it.
extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");

// App image lives at this offset inside the merged bin (matches the app0/ota_0
// partition offset on both boards and merge_firmware.py's firmware_offset).
static constexpr uint32_t APP_OFFSET = 0x10000;

#ifdef PLATFORM_TWATCH
static const char* FW_PREFIX = "mclite-watch-v";
#else
static const char* FW_PREFIX = "mclite-v";
#endif

bool FirmwareUpdater::matchName(const String& base, String& versionOut) {
    if (base.startsWith("._")) return false;          // macOS resource fork
    if (!base.startsWith(FW_PREFIX)) return false;    // wrong board / not ours
    if (!base.endsWith(".bin")) return false;         // excludes ".bin.installed"
    String v = base.substring(strlen(FW_PREFIX), base.length() - 4 /* ".bin" */);
    if (v.length() == 0) return false;
    versionOut = v;
    return true;
}

String FirmwareUpdater::findSdFirmware(bool autoMode, String& outVersion) {
    if (!SDCard::instance().isMounted()) return "";

    const char* dirs[] = {"/", "/firmware"};
    String bestPath, bestVer;

    for (const char* d : dirs) {
        if (!SD.exists(d)) continue;  // skip absent /firmware quietly (no vfs error log)
        File dir = SD.open(d);
        if (!dir) continue;
        if (!dir.isDirectory()) { dir.close(); continue; }

        File f = dir.openNextFile();
        while (f) {
            String name = f.name();
            bool isDir = f.isDirectory();
            f.close();  // close before openNextFile() — ESP32 file-handle limit

            int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);

            String ver;
            if (!isDir && matchName(name, ver)) {
                if (bestVer.length() == 0 || compareVersions(ver.c_str(), bestVer.c_str()) > 0) {
                    bestVer = ver;
                    String full = String(d);
                    if (!full.endsWith("/")) full += "/";
                    full += name;
                    bestPath = full;
                }
            }
            f = dir.openNextFile();
        }
        dir.close();
    }

    if (bestPath.length() == 0) return "";
    // Auto-boot path: skip when the best candidate is the running version (loop guard).
    if (autoMode && compareVersions(bestVer.c_str(), MCLITE_VERSION) == 0) return "";

    outVersion = bestVer;
    return bestPath;
}

bool FirmwareUpdater::flashFromSd(const char* path, ProgressCb cb, void* user) {
    File f = SDCard::instance().openRaw(path);
    if (!f) { LOGF("[OTA] open failed: %s\n", path); return false; }

    size_t total = f.size();
    if (total <= APP_OFFSET) {
        LOGF("[OTA] file too small (%u bytes)\n", (unsigned)total);
        f.close();
        return false;
    }

    // Validate the ESP32 app-image magic byte (0xE9) at the app offset so we
    // never feed a non-firmware .bin to Update.
    if (!f.seek(APP_OFFSET)) { f.close(); return false; }
    uint8_t magic = 0;
    if (f.read(&magic, 1) != 1 || magic != 0xE9) {
        LOGF("[OTA] bad image magic 0x%02X at 0x%X\n", magic, (unsigned)APP_OFFSET);
        f.close();
        return false;
    }

    // --- SHA-256 integrity check: require a <path>.sha256 sidecar file ---
    {
        String hashPath = String(path) + ".sha256";
        File hf = SD.open(hashPath.c_str());
        if (!hf) {
            LOGF("[OTA] SHA-256 sidecar missing: %s\n", hashPath.c_str());
            f.close();
            return false;
        }
        char expectedHex[65] = {};
        hf.readBytes(expectedHex, 64);
        hf.close();
        for (char* p = expectedHex + 63; p >= expectedHex && (*p == '\n' || *p == '\r' || *p == ' '); --p)
            *p = '\0';
        if (strlen(expectedHex) != 64) {
            LOGLN("[OTA] SHA-256 sidecar format invalid");
            f.close();
            return false;
        }
        mbedtls_sha256_context ctx;
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts(&ctx, 0 /* SHA-256 */);
        f.seek(0);
        uint8_t tmp[4096];
        while (f.available()) {
            int n = f.read(tmp, sizeof(tmp));
            if (n > 0) mbedtls_sha256_update(&ctx, tmp, n);
        }
        uint8_t digest[32];
        mbedtls_sha256_finish(&ctx, digest);
        mbedtls_sha256_free(&ctx);
        char actualHex[65] = {};
        for (int i = 0; i < 32; ++i) snprintf(actualHex + i * 2, 3, "%02x", digest[i]);
        if (strncasecmp(actualHex, expectedHex, 64) != 0) {
            LOGF("[OTA] SHA-256 mismatch: got %s, expected %s\n", actualHex, expectedHex);
            f.close();
            return false;
        }
        LOGLN("[OTA] SHA-256 verified");
    }

    size_t appSize = total - APP_OFFSET;
    if (!Update.begin(appSize)) {
        LOGF("[OTA] Update.begin failed: %s\n", Update.errorString());
        f.close();
        return false;
    }

    f.seek(APP_OFFSET);
    const size_t CHUNK = 4096;
    static uint8_t buf[CHUNK];   // static: keep this 4 KB off the (TLS-heavy) loop-task stack
    size_t written = 0;
    uint8_t lastPct = 255;
    int sinceYield = 0;

    while (written < appSize) {
        size_t want = appSize - written;
        if (want > CHUNK) want = CHUNK;
        int n = f.read(buf, want);
        if (n <= 0) {
            LOGLN("[OTA] SD read error");
            Update.abort();
            f.close();
            return false;
        }
        if (Update.write(buf, n) != (size_t)n) {
            LOGF("[OTA] write error: %s\n", Update.errorString());
            Update.abort();
            f.close();
            return false;
        }
        written += n;

        uint8_t pct = (uint8_t)((written * 100) / appSize);
        if (cb && pct != lastPct) { cb(pct, user); lastPct = pct; }

        // Feed the watchdog / let other tasks run during the multi-second flash.
        yield();
        if (++sinceYield >= 16) { delay(1); sinceYield = 0; }
    }

    f.close();

    if (!Update.end(true) || !Update.isFinished()) {
        LOGF("[OTA] finalize failed: %s\n", Update.errorString());
        return false;
    }

    // Rename so the freshly-booted firmware won't re-detect and re-prompt.
    String installed = String(path) + ".installed";
    if (SD.exists(installed.c_str())) SD.remove(installed.c_str());  // clear stale marker (quietly)
    if (!SD.rename(path, installed.c_str())) {
        LOGF("[OTA] rename failed (%s) — version gate will guard the loop\n", path);
    }

    LOGLN("[OTA] flash OK — rebooting into new firmware");
    return true;
}

bool FirmwareUpdater::downloadToSd(const char* url, const char* destPath,
                                   ProgressCb cb, void* user) {
    if (!SDCard::instance().isMounted()) return false;
    SDCard::instance().mkdir("/firmware");  // no-op if it already exists

    WiFiClientSecure client;
    client.setCACertBundle(rootca_crt_bundle_start);

    HTTPClient http;
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);  // GitHub → CDN redirect
    http.setUserAgent("MCLite");
    if (!http.begin(client, url)) { LOGLN("[OTA] http.begin failed"); return false; }

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        LOGF("[OTA] download HTTP %d\n", code);
        http.end();
        return false;
    }

    int total = http.getSize();  // -1 if the server didn't send Content-Length
    File f = SD.open(destPath, FILE_WRITE);
    if (!f) { LOGF("[OTA] cannot open %s for write\n", destPath); http.end(); return false; }

    WiFiClient* stream = http.getStreamPtr();
    const size_t CHUNK = 4096;
    static uint8_t buf[CHUNK];   // static: keep this 4 KB off the (TLS-heavy) loop-task stack
    int written = 0;
    uint8_t lastPct = 255;
    int sinceYield = 0;

    while (http.connected() && (total < 0 || written < total)) {
        size_t avail = stream->available();
        if (avail) {
            int n = stream->readBytes(buf, avail > CHUNK ? CHUNK : avail);
            if (n <= 0) break;
            if (f.write(buf, n) != (size_t)n) {
                LOGLN("[OTA] SD write short");
                f.close(); http.end(); SD.remove(destPath);
                return false;
            }
            written += n;
            if (cb && total > 0) {
                uint8_t p = (uint8_t)(((int64_t)written * 100) / total);
                if (p != lastPct) { cb(p, user); lastPct = p; }
            }
        } else {
            delay(2);
        }
        yield();
        if (++sinceYield >= 16) { delay(1); sinceYield = 0; }
    }

    f.close();
    http.end();

    if ((total > 0 && written < total) || written <= 0x10000) {
        LOGF("[OTA] download incomplete (%d/%d)\n", written, total);
        SD.remove(destPath);
        return false;
    }
    LOGF("[OTA] downloaded %d bytes -> %s\n", written, destPath);
    return true;
}

}  // namespace mclite
