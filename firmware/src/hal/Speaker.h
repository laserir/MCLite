#pragma once

#include <cstdint>

#define SPEAKER_VOLUME_SOS            80  // +4.10dB, 1.30x perceived loudness
#define SPEAKER_VOLUME_CHIME_MAX      50  //  0.00dB, 1.00x perceived loudness
#define SPEAKER_VOLUME_CHIME_MID      16  // -9.90dB, 0.50x perceived loudness
#define SPEAKER_VOLUME_CHIME_MUTE     0

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

    bool isVolumeMax() const { return _volume == SPEAKER_VOLUME_CHIME_MAX; }
    bool isVolumeMid() const { return _volume == SPEAKER_VOLUME_CHIME_MID; }
    
    void toggleVolume() {
        if (_volume == SPEAKER_VOLUME_CHIME_MUTE) {
            _volume = SPEAKER_VOLUME_CHIME_MID;
        } else if (_volume == SPEAKER_VOLUME_CHIME_MID) {
            _volume = SPEAKER_VOLUME_CHIME_MAX;
        } else if (_volume == SPEAKER_VOLUME_CHIME_MAX) {
            _volume = SPEAKER_VOLUME_CHIME_MUTE;
        }
    }

    void setMuted() { _volume = SPEAKER_VOLUME_CHIME_MUTE; }
    bool isMuted() const { return _volume == SPEAKER_VOLUME_CHIME_MUTE; }

    static Speaker& instance();

private:
    Speaker() = default;
    bool _initialized = false;
    bool _hasCustomSound = false;
    bool _hasSOSWav      = false;
    uint8_t _volume = SPEAKER_VOLUME_CHIME_MAX;  // boot at max so upgrades keep current loudness; bell steps down

    // SOS repeat state
    uint8_t  _sosRepeatsRemaining = 0;
    uint32_t _sosNextPlayMs       = 0;
    bool     _sosCheckedWav       = false;

    // Built-in two-tone ascending chime (iMessage-style)
    void playBuiltinChime(uint8_t volume);

    // Built-in morse SOS pattern (urgent 2000 Hz)
    void playBuiltinSOS();

    // Play a WAV file from SD card
    bool playWavFile(const char* path, uint8_t volume);

    // Generate a sine tone into the I2S buffer
    void writeTone(uint16_t freqHz, uint16_t durationMs, uint8_t volume);
    void writeSilence(uint16_t durationMs);
};

}  // namespace mclite
