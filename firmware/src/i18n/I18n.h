#pragma once
#include <Arduino.h>
#include "../config/defaults.h"

namespace mclite {

class I18n {
public:
    // Load translation file from SD. Call once at boot.
    // If lang is empty or file missing, English (no-op).
    void init(const String& langCode);

    // Translate a key. Returns English fallback if key not found.
    const char* t(const char* key);

    // Translate a key that will be handed to printf/snprintf as the FORMAT string.
    // Lang files are user-editable JSON on the SD card, so a translation can carry
    // conversion specifiers that disagree with the arguments the caller passes --
    // undefined behaviour, ranging from garbage in a dialog (a %d printing a
    // pointer) to a crash (a %s dereferencing an integer). It also happens to any
    // device whose SD lang files are older than its firmware, which is the normal
    // state after a USB flash. tf() returns the translation only when its
    // specifiers match the English default exactly, and the English default
    // otherwise -- worst case the user sees one line in English instead of a
    // corrupted line, or a reboot.
    const char* tf(const char* key);

    // True when a translation file is loaded and predates this firmware's string
    // set. Detected at boot; acted on when a network is available (the OTA path
    // can re-download the files), rather than only logged to a serial port nobody
    // is watching.
    // _count > 0 means a file was actually loaded; version 0 means the file
    // predates the "version" field (before 0.3.9), which is stale by definition
    // -- the earlier `> 0` test skipped exactly the oldest files.
    bool langNeedsRefresh() const { return _count > 0 &&
                                           _langFileVersion < (int)defaults::LANG_VERSION; }
    int  langFileVersion() const { return _langFileVersion; }

    const String& currentLanguage() const { return _currentLang; }
    const String& availableLanguages() const { return _availableLangs; }

    static I18n& instance();

private:
    I18n() = default;
    // Must stay above the number of keys in the largest SD lang file (~268 today).
    // When exceeded, the loader silently truncates and every key past the cap
    // falls back to English — keep generous headroom as strings are added.
    // Must stay above the DEFAULT_STRINGS count (323 today). The loader drops
    // everything past this cap, so an undersized value silently leaves the last
    // keys in file order untranslated -- exactly what happened when the count
    // grew past 320. Headroom is cheap: sizeof(Entry) bytes of .bss per slot.
    static constexpr size_t MAX_STRINGS = 400;

    // English default for `key`, or nullptr if the key is unknown.
    static const char* englishFor(const char* key);
    // Do two format strings carry the same conversion specifiers, in order?
    static bool formatSpecsMatch(const char* a, const char* b);

    struct Entry { const char* key; const char* value; };
    Entry _entries[MAX_STRINGS];
    size_t _count = 0;
    char* _jsonBuf = nullptr;  // Single allocation owning all key+value strings
    int _langFileVersion = 0;   // "version" of the loaded lang file; 0 = English/none
    String _currentLang = "en";
    String _availableLangs = "en";
};

// Shorthand global function
inline const char* t(const char* key) { return I18n::instance().t(key); }
// Use this, never t(), when the result is a printf/snprintf FORMAT string.
inline const char* tf(const char* key) { return I18n::instance().tf(key); }

}  // namespace mclite
