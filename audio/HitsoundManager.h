#pragma once

#include <string>
#include <vector>
#include <cstdint>

// Minimal manager: just loads WAV samples + stores timestamps.
// AudioEngine does the actual mixing during playback.
class HitsoundManager {
public:
    void init(const std::string& assetsDir = "");
    void setHitsoundType(const std::string& type);
    bool prepare(const std::vector<double>& timestamps);

    const float*  samples() const { return m_samples.data(); }
    int    sampleCount() const { return m_sampleCount; }
    int    sampleRate()  const { return m_sampleRate; }
    const double* timestamps() const { return m_timestamps.data(); }
    int    hitCount() const { return (int)m_timestamps.size(); }

private:
    std::string m_assetsDir;
    std::string m_hitsoundType = "Kick";

    std::vector<float>  m_samples;    // decoded WAV PCM (mono)
    std::vector<double> m_timestamps; // sorted seconds from start
    int m_sampleCount = 0;
    int m_sampleRate = 44100;

    std::string hitsoundPath(const std::string& type) const;
    bool readWav(const std::string& filepath, std::vector<float>& samples, int& sr, int& channels);
};
