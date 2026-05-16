#pragma once

#include <string>
#include <vector>
#include <cstdint>

// Pre-synthesizes all hitsounds into a single stereo float buffer.
// Matches Re_ADOJAS / ADOFAN_PIXI approach: mix once at load time, stream at runtime.
class HitsoundManager {
public:
    void init(const std::string& assetsDir = "");
    void setHitsoundType(const std::string& type);
    void setVolume(float vol) { m_volume = vol; }

    // Pre-synthesize hits into stereo buffer.  timestamps in seconds from space-press.
    bool preSynthesize(const std::vector<double>& timestamps, float totalDuration);

    const float* buffer() const { return m_buffer.data(); }
    int    bufferFrameCount() const { return m_bufferFrameCount; }  // stereo frames
    int    sampleRate()      const { return 44100; }

private:
    std::string m_assetsDir;
    std::string m_hitsoundType = "Kick";
    float       m_volume = 1.0f;  // 0.0–1.0

    std::vector<float> m_buffer;         // pre-mixed stereo interleaved
    int m_bufferFrameCount = 0;

    std::string hitsoundPath(const std::string& type) const;
    bool readWav(const std::string& filepath, std::vector<float>& samples, int& sr, int& channels);
    static void softClip(float* buf, int n);
};
