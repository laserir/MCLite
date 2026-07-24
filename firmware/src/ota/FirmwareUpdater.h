#pragma once

#include <Arduino.h>

// On-device firmware install from an SD-card .bin (no networking).
//
// The web flasher / GitHub releases distribute a MERGED image
// (bootloader@0x0 + partitions@0x8000 + app@0x10000). The ESP32 Update/OTA API
// wants only the app image, which is exactly the merged file from offset
// 0x10000 to EOF — so we seek there and stream the rest into the inactive OTA
// slot. No separate artifact and no partition-table change are needed (both
// boards already ship a dual-OTA layout).
//
// Board guard: T-Deck installs only "mclite-v*.bin", T-Watch only
// "mclite-watch-v*.bin" (handled by FW_PREFIX below).

namespace mclite {

class FirmwareUpdater {
public:
    using ProgressCb = void (*)(uint8_t percent, void* user);

    // Scan the SD card ("/" and "/firmware") for a board-matching firmware bin.
    //  - autoMode=true : returns "" if the best candidate equals MCLITE_VERSION
    //                    (so the just-installed build won't re-prompt on boot).
    //  - autoMode=false: returns any board-matching bin (manual / recovery).
    // Returns the full SD path of the highest-version match, or "" if none.
    // On success, outVersion is set to the parsed version string.
    static String findSdFirmware(bool autoMode, String& outVersion);

    // Flash the app image out of a merged firmware bin and, on success, rename
    // the file to "<name>.installed" so the rebooted firmware won't re-detect it.
    // Blocking; the caller should ESP.restart() after a true return.
    static bool flashFromSd(const char* path, ProgressCb cb = nullptr, void* user = nullptr);

    // Download a merged firmware bin from `url` (HTTPS, GitHub) to `destPath` on
    // the SD card. Requires an active WiFi connection. TLS is validated against
    // the built-in root-CA bundle. Blocking; returns true on a complete download.
    // The caller then hands `destPath` to flashFromSd().
    static bool downloadToSd(const char* url, const char* destPath,
                             ProgressCb cb = nullptr, void* user = nullptr);

    // Refresh the SD-card translation files (/mclite/lang/<code>.json) to match the
    // firmware `version` just installed, fetching each language ALREADY PRESENT on the
    // SD from raw.githubusercontent.com pinned to the "v<version>" tag. This closes the
    // gap where a firmware OTA adds i18n keys but the SD lang files stay stale (falling
    // back to English). Best-effort and non-fatal: a failed HTTP GET or an invalid JSON
    // body leaves the existing file untouched; new languages are never added. Requires an
    // active WiFi connection. Blocking; returns the number of files updated.
    static int refreshLangFiles(const String& version);

private:
    static bool matchName(const String& base, String& versionOut);
};

}  // namespace mclite
