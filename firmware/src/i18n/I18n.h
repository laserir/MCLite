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
    static constexpr size_t MAX_STRINGS = 128;

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
