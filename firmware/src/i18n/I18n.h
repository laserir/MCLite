#pragma once
#include <Arduino.h>

namespace mclite {

class I18n {
public:
    // Load translation file from SD. Call once at boot.
    // If lang is empty or file missing, English (no-op).
    void init(const String& langCode);

    // Translate a key. Returns English fallback if key not found.
    const char* t(const char* key);

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

    struct Entry { const char* key; const char* value; };
    Entry _entries[MAX_STRINGS];
    size_t _count = 0;
    char* _jsonBuf = nullptr;  // Single allocation owning all key+value strings
    String _currentLang = "en";
    String _availableLangs = "en";
};

// Shorthand global function
inline const char* t(const char* key) { return I18n::instance().t(key); }

}  // namespace mclite
