#pragma once

#include <string>
#include <vector>
#include <functional>

using HitsoundProgressCb = std::function<void(float percent)>;

class HitsoundManager {
public:
    HitsoundManager();
    ~HitsoundManager();

    HitsoundManager(const HitsoundManager&) = delete;
    HitsoundManager& operator=(const HitsoundManager&) = delete;

    void init(const std::string& assetsDir = "");

    void setHitsoundType(const std::string& type);
    void setVolume(float vol);  // 0-100
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    bool preSynthesize(const std::vector<float>& timestamps, float totalDuration,
                       HitsoundProgressCb onProgress = nullptr);

    // Read-only access for mixer
    const float* buffer() const { return m_buffer.data(); }
    size_t totalFrames() const { return m_buffer.size() / 2; }
    int channels() const { return 2; }
    int sampleRate() const { return m_sampleRate; }
    size_t* cursor() { return &m_readCursor; }
    bool* playing() { return &m_playing; }

    void reset();
    void stop();
    bool isSynthesized() const { return m_synthesized; }

private:
    std::string m_assetsDir;

    std::vector<float> m_buffer;
    int m_sampleRate = 44100;
    size_t m_readCursor = 0;

    std::string m_hitsoundType = "Kick";
    float m_volume = 1.0f;
    bool m_enabled = true;
    bool m_synthesized = false;
    bool m_playing = false;

    std::string hitsoundPath(const std::string& type) const;
    bool readWav(const std::string& filepath,
                 std::vector<float>& samples,
                 int& sampleRate, int& channels);
};
