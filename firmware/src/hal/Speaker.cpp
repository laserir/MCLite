#include "Speaker.h"
#include "../storage/SDCard.h"
#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>

namespace mclite {

static constexpr i2s_port_t I2S_PORT = I2S_NUM_0;
static constexpr uint32_t SAMPLE_RATE = 16000;
static constexpr const char* CUSTOM_SOUND_PATH = "/notification.wav";
static constexpr const char* SOS_SOUND_PATH   = "/sos.wav";

Speaker& Speaker::instance() {
    static Speaker inst;
    return inst;
}

bool Speaker::init() {
    i2s_config_t i2s_config = {};
    i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    i2s_config.sample_rate = SAMPLE_RATE;
    i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    i2s_config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    i2s_config.dma_buf_count = 4;
    i2s_config.dma_buf_len = 256;
    i2s_config.use_apll = false;
    i2s_config.tx_desc_auto_clear = true;

    if (i2s_driver_install(I2S_PORT, &i2s_config, 0, nullptr) != ESP_OK) {
        Serial.println("[Speaker] I2S driver install failed");
        return false;
    }

    i2s_pin_config_t pin_config = {};
    pin_config.mck_io_num = I2S_PIN_NO_CHANGE;   // Don't use MCLK (default 0 would steal GPIO 0!)
    pin_config.bck_io_num = TDECK_I2S_BCK;
    pin_config.ws_io_num = TDECK_I2S_WS;
    pin_config.data_out_num = TDECK_I2S_DOUT;
    pin_config.data_in_num = I2S_PIN_NO_CHANGE;

    if (i2s_set_pin(I2S_PORT, &pin_config) != ESP_OK) {
        Serial.println("[Speaker] I2S pin config failed");
        i2s_driver_uninstall(I2S_PORT);
        return false;
    }

    // Check if custom sound file exists on SD
    _hasCustomSound = SDCard::instance().fileExists(CUSTOM_SOUND_PATH);
    if (_hasCustomSound) {
        Serial.println("[Speaker] Custom notification.wav found");
    }

    _initialized = true;
    Serial.println("[Speaker] I2S ready");
    return true;
}

void Speaker::playNotification() {
    if (!_initialized || _muted) return;

    if (_hasCustomSound && playWavFile(CUSTOM_SOUND_PATH)) {
        return;  // Custom sound played successfully
    }

    // Fall back to built-in chime
    playBuiltinChime();
}

void Speaker::playNotificationForced() {
    if (!_initialized) return;

    if (_hasCustomSound && playWavFile(CUSTOM_SOUND_PATH)) {
        return;
    }
    playBuiltinChime();
}

void Speaker::startSOS(uint8_t repeatCount) {
    if (!_initialized || repeatCount == 0) return;

    // Check for custom SOS WAV on first use
    if (!_sosCheckedWav) {
        _hasSOSWav = SDCard::instance().fileExists(SOS_SOUND_PATH);
        _sosCheckedWav = true;
        if (_hasSOSWav) Serial.println("[Speaker] Custom sos.wav found");
    }

    _sosRepeatsRemaining = repeatCount;
    _sosNextPlayMs = 0;  // Play immediately on next update()
}

void Speaker::stopSOS() {
    _sosRepeatsRemaining = 0;
    _sosNextPlayMs = 0;
}

void Speaker::update() {
    if (_sosRepeatsRemaining == 0) return;
    if (_sosNextPlayMs != 0 && (int32_t)(millis() - _sosNextPlayMs) < 0) return;

    // Play one SOS cycle
    if (_hasSOSWav && playWavFile(SOS_SOUND_PATH)) {
        // Custom WAV played
    } else {
        playBuiltinSOS();
    }

    _sosRepeatsRemaining--;
    if (_sosRepeatsRemaining > 0) {
        _sosNextPlayMs = millis() + 500;  // 500ms pause between repeats
    }
}

void Speaker::playBuiltinSOS() {
    // Morse SOS: ... --- ... at 2000 Hz urgent tone
    // Three short beeps
    for (int i = 0; i < 3; i++) {
        writeTone(2000, 100, 80);
        writeSilence(50);
    }
    // Three long beeps
    for (int i = 0; i < 3; i++) {
        writeTone(2000, 300, 80);
        writeSilence(50);
    }
    // Three short beeps
    for (int i = 0; i < 3; i++) {
        writeTone(2000, 100, 80);
        writeSilence(50);
    }
}

void Speaker::writeTone(uint16_t freqHz, uint16_t durationMs, uint8_t volume) {
    uint32_t totalSamples = (SAMPLE_RATE * durationMs) / 1000;
    int16_t amplitude = (int16_t)((32767 * volume) / 255);

    static constexpr size_t CHUNK = 256;
    int16_t buf[CHUNK];

    for (uint32_t i = 0; i < totalSamples; i += CHUNK) {
        size_t count = min((uint32_t)CHUNK, totalSamples - i);
        for (size_t j = 0; j < count; j++) {
            // Sine wave for a pleasant tone
            float phase = 2.0f * M_PI * freqHz * (float)(i + j) / SAMPLE_RATE;
            // Apply fade envelope (attack 10ms, release last 20%)
            float env = 1.0f;
            float pos = (float)(i + j) / totalSamples;
            if (pos < 0.05f) env = pos / 0.05f;           // Attack
            else if (pos > 0.8f) env = (1.0f - pos) / 0.2f; // Release
            buf[j] = (int16_t)(sinf(phase) * amplitude * env);
        }
        size_t bytesWritten = 0;
        i2s_write(I2S_PORT, buf, count * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
    }
}

void Speaker::writeSilence(uint16_t durationMs) {
    uint32_t totalSamples = (SAMPLE_RATE * durationMs) / 1000;
    static constexpr size_t CHUNK = 256;
    int16_t buf[CHUNK];
    memset(buf, 0, sizeof(buf));

    for (uint32_t i = 0; i < totalSamples; i += CHUNK) {
        size_t count = min((uint32_t)CHUNK, totalSamples - i);
        size_t bytesWritten = 0;
        i2s_write(I2S_PORT, buf, count * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
    }
}

void Speaker::playBuiltinChime() {
    // Two-tone ascending chime (iMessage-inspired)
    // Note 1: E6 (1318 Hz), 80ms
    // Note 2: A6 (1760 Hz), 120ms — slight overlap feel
    writeTone(1318, 80, 50);
    writeSilence(30);
    writeTone(1760, 120, 50);
    writeSilence(20);  // Flush DMA
}

bool Speaker::playWavFile(const char* path) {
    // Simple WAV parser: expects 16-bit PCM, mono or stereo
    auto& sd = SDCard::instance();
    if (!sd.isMounted()) return false;

    File file = SD.open(path, FILE_READ);
    if (!file) return false;

    // Read WAV header (44 bytes minimum)
    uint8_t header[44];
    if (file.read(header, 44) != 44) {
        file.close();
        return false;
    }

    // Validate RIFF header
    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        file.close();
        return false;
    }

    // Parse format chunk
    uint16_t audioFormat = header[20] | (header[21] << 8);
    uint16_t numChannels = header[22] | (header[23] << 8);
    uint32_t wavSampleRate = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
    uint16_t bitsPerSample = header[34] | (header[35] << 8);

    // Only support PCM (format 1), 16-bit, reasonable sample rate
    if (audioFormat != 1 || bitsPerSample != 16 || wavSampleRate > 48000 || wavSampleRate == 0 || numChannels == 0 || numChannels > 2) {
        Serial.println("[Speaker] WAV format not supported (need 16-bit PCM, 1-2ch, <= 48kHz)");
        file.close();
        return false;
    }

    // Seek to "data" chunk — may not be at offset 44 if extra chunks exist
    file.seek(12);  // Skip RIFF header + "WAVE"
    bool foundData = false;
    while (file.available()) {
        uint8_t chunkHdr[8];
        if (file.read(chunkHdr, 8) != 8) { file.close(); return false; }
        uint32_t chunkSize = chunkHdr[4] | (chunkHdr[5] << 8) | (chunkHdr[6] << 16) | (chunkHdr[7] << 24);
        if (memcmp(chunkHdr, "data", 4) == 0) { foundData = true; break; }
        if (chunkSize == 0) break;  // Malformed chunk — avoid infinite loop
        file.seek(file.position() + chunkSize + (chunkSize & 1));  // Skip chunk + RIFF pad byte
    }
    if (!foundData) {
        Serial.println("[Speaker] WAV file missing data chunk");
        file.close();
        return false;
    }

    // Adjust I2S sample rate to match WAV file
    i2s_set_sample_rates(I2S_PORT, wavSampleRate);

    // Stream audio data in chunks (limit to 3 seconds max)
    uint32_t maxBytes = wavSampleRate * numChannels * 2 * 3;  // 3 seconds
    uint32_t bytesPlayed = 0;

    static constexpr size_t BUF_SIZE = 512;
    int16_t buf[BUF_SIZE];

    while (bytesPlayed < maxBytes) {
        int bytesRead = file.read((uint8_t*)buf, BUF_SIZE * sizeof(int16_t));
        if (bytesRead <= 0) break;

        // If stereo, mix down to mono in-place
        if (numChannels == 2) {
            int16_t* src = buf;
            int samples = bytesRead / 4;  // 2 channels * 2 bytes
            for (int i = 0; i < samples; i++) {
                buf[i] = (src[i * 2] / 2) + (src[i * 2 + 1] / 2);
            }
            size_t bytesWritten = 0;
            i2s_write(I2S_PORT, buf, samples * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
        } else {
            size_t bytesWritten = 0;
            i2s_write(I2S_PORT, buf, bytesRead, &bytesWritten, portMAX_DELAY);
        }

        bytesPlayed += bytesRead;
    }

    file.close();

    // Restore default sample rate
    i2s_set_sample_rates(I2S_PORT, SAMPLE_RATE);

    // Flush with silence
    writeSilence(20);

    return true;
}

}  // namespace mclite
