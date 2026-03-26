#pragma once

#include <cstdint>

namespace mclite {

class Speaker {
public:
    bool init();

    // Play the notification sound (custom WAV from SD if available, else built-in chime)
    void playNotification();

    // Play notification bypassing mute (for always-sound contacts)
    void playNotificationForced();

    // SOS alert sound (repeating urgent pattern)
    void startSOS(uint8_t repeatCount);
    void stopSOS();
    void update();  // Call from main loop for non-blocking SOS repeat

    // Mute/unmute (toggleable from status bar bell icon)
    void setMuted(bool muted) { _muted = muted; }
    bool isMuted() const { return _muted; }
    void toggleMute() { _muted = !_muted; }

    static Speaker& instance();

private:
    Speaker() = default;
    bool _initialized = false;
    bool _muted       = false;
    bool _hasCustomSound = false;
    bool _hasSOSWav      = false;

    // SOS repeat state
    uint8_t  _sosRepeatsRemaining = 0;
    uint32_t _sosNextPlayMs       = 0;
    bool     _sosCheckedWav       = false;

    // Built-in two-tone ascending chime (iMessage-style)
    void playBuiltinChime();

    // Built-in morse SOS pattern (urgent 2000 Hz)
    void playBuiltinSOS();

    // Play a WAV file from SD card
    bool playWavFile(const char* path);

    // Generate a sine tone into the I2S buffer
    void writeTone(uint16_t freqHz, uint16_t durationMs, uint8_t volume);
    void writeSilence(uint16_t durationMs);
};

}  // namespace mclite
