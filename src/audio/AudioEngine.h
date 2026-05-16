#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct ma_device;
struct stb_vorbis;

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool init();
    void shutdown();

    bool loadMusic(const std::string& filepath);
    void play();
    void pause();
    void stop();
    void resume();
    void seek(float seconds);

    float position() const;
    float duration() const;
    bool isPlaying() const { return m_playing; }
    bool hasMusic() const { return m_hasMusic; }

    void setVolume(float v);
    bool hasHitsounds() const { return m_hitSamples != nullptr; }

    // Attach hitsound data for real-time mixing in callback
    void attachHitsounds(const float* samples, int sampleCount, int sampleRate,
                         const double* timestamps, int hitCount);
    void setHitBaseTime();  // record current audio position as t=0 for timestamps

private:
    ma_device* m_device = nullptr;
    stb_vorbis* m_vorbis = nullptr;
    std::vector<uint8_t> m_fileData;
    int m_sampleRate = 44100;
    int m_channels = 2;
    float m_duration = 0.0f;
    float m_volume = 1.0f;
    bool m_initialized = false;
    bool m_hasMusic = false;
    bool m_playing = false;

    // Hitsound real-time scheduling
    const float*  m_hitSamples = nullptr;
    int           m_hitSampleCount = 0;
    int           m_hitSampleRate = 44100;
    const double* m_hitTimestamps = nullptr;
    int           m_hitCount = 0;
    int           m_hitCursor = 0;
    double        m_hitBaseTime = 0.0;
    uint64_t      m_totalFrames = 0;     // accumulated callback frames (clock when no music)

    static void dataCallback(ma_device* pDevice, void* pOutput, const void*, unsigned int frameCount);
};
