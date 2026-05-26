#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct ma_device;
struct ma_decoder;

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

    // External audio source for mixing (hitsounds)
    void attachExternal(const float* buffer, size_t totalFrames, int channels, int sampleRate,
                        size_t* cursor, bool* playing);
    void detachExternal();

private:
    ma_device* m_device = nullptr;
    ma_decoder* m_decoder = nullptr;
    std::vector<uint8_t> m_fileData;  // keep file memory alive for decoder
    int m_sampleRate = 44100;
    uint64_t m_readCursor = 0;  // current decoder position in frames
    float m_duration = 0.0f;
    float m_volume = 1.0f;
    bool m_initialized = false;
    bool m_hasMusic = false;
    bool m_playing = false;

    // External source (hitsounds)
    const float* m_extBuffer = nullptr;
    size_t m_extTotalFrames = 0;
    int m_extChannels = 0;
    int m_extSampleRate = 44100;
    size_t* m_extCursor = nullptr;
    bool* m_extPlaying = nullptr;

    static void dataCallback(ma_device* pDevice, void* pOutput, const void*, unsigned int frameCount);
};
