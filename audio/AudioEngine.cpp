#include "AudioEngine.h"
#include "util/Logger.h"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::init() {
#ifdef _WIN32
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif

    m_device = new ma_device;
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_f32;
    config.playback.channels = 2;
    config.sampleRate        = 44100;
    config.dataCallback      = dataCallback;
    config.pUserData         = this;

    if (ma_device_init(nullptr, &config, m_device) != MA_SUCCESS) {
        config.sampleRate = 0;
        if (ma_device_init(nullptr, &config, m_device) != MA_SUCCESS) {
            LOG_E("AudioEngine: Failed to init audio device");
            delete m_device;
            m_device = nullptr;
            return false;
        }
    }

    m_initialized = true;
    LOG_I("AudioEngine: Initialized (%dHz, backend=%s)",
          (int)m_device->playback.internalSampleRate,
          ma_get_backend_name(m_device->pContext->backend));
    return true;
}

void AudioEngine::shutdown() {
    if (m_device) {
        if (m_device->pContext) ma_device_uninit(m_device);
        delete m_device;
        m_device = nullptr;
    }
    if (m_vorbis) {
        stb_vorbis_close(m_vorbis);
        m_vorbis = nullptr;
    }
    m_fileData.clear();
    m_initialized = false;
    m_hasMusic = false;
    m_playing = false;
}

bool AudioEngine::loadMusic(const std::string& filepath) {
    if (!m_device) return false;

    if (m_vorbis) {
        stb_vorbis_close(m_vorbis);
        m_vorbis = nullptr;
        m_hasMusic = false;
    }
    m_fileData.clear();

    // Read file into memory (handles UTF-8 paths on Windows)
    std::vector<uint8_t> data;
    {
#ifdef _WIN32
        int wlen = MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), -1, nullptr, 0);
        if (wlen <= 0) { LOG_E("AudioEngine: Invalid path"); return false; }
        std::wstring wpath(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, filepath.c_str(), -1, &wpath[0], wlen);

        FILE* f = _wfopen(wpath.c_str(), L"rb");
        if (!f) { LOG_E("AudioEngine: Cannot open file"); return false; }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz <= 0) { fclose(f); return false; }
        data.resize((size_t)sz);
        fread(data.data(), 1, (size_t)sz, f);
        fclose(f);
#else
        std::ifstream f(filepath, std::ios::binary | std::ios::ate);
        if (!f) { LOG_E("AudioEngine: Cannot open file"); return false; }
        auto sz = f.tellg();
        f.seekg(0);
        data.resize((size_t)sz);
        f.read((char*)data.data(), sz);
#endif
    }

    int error = 0;
    m_vorbis = stb_vorbis_open_memory(data.data(), (int)data.size(), &error, nullptr);
    if (!m_vorbis) {
        LOG_E("AudioEngine: Failed to decode OGG [stb_err=%d]: %s", error, filepath.c_str());
        return false;
    }
    m_fileData = std::move(data);  // keep data alive for vorbis

    stb_vorbis_info info = stb_vorbis_get_info(m_vorbis);
    m_sampleRate = info.sample_rate;
    m_channels = info.channels;

    unsigned int totalSamples = stb_vorbis_stream_length_in_samples(m_vorbis);
    m_duration = (m_sampleRate > 0) ? (float)totalSamples / (float)m_sampleRate : 0.0f;

    m_hasMusic = true;
    LOG_I("AudioEngine: Loaded (%dHz, %dch, %.1fs): %s",
          m_sampleRate, m_channels, m_duration, filepath.c_str());
    return true;
}

void AudioEngine::play() {
    if (!m_device) return;
    m_playing = true;
    if (ma_device_is_started(m_device) == MA_FALSE)
        ma_device_start(m_device);
}

void AudioEngine::resume() {
    if (!m_device || !m_hasMusic) return;
    m_playing = true;
    if (ma_device_is_started(m_device) == MA_FALSE)
        ma_device_start(m_device);
}

void AudioEngine::pause() {
    m_playing = false;
}

void AudioEngine::stop() {
    m_playing = false;
    if (m_vorbis) stb_vorbis_seek_start(m_vorbis);
    if (m_device && ma_device_is_started(m_device) != MA_FALSE)
        ma_device_stop(m_device);
}

void AudioEngine::seek(float seconds) {
    if (!m_vorbis || m_sampleRate <= 0) return;
    unsigned int sample = (unsigned int)(seconds * (float)m_sampleRate);
    stb_vorbis_seek(m_vorbis, sample);
}

float AudioEngine::position() const {
    if (!m_vorbis || m_sampleRate <= 0) return 0.0f;
    return (float)stb_vorbis_get_sample_offset(m_vorbis) / (float)m_sampleRate;
}

float AudioEngine::duration() const {
    return m_duration;
}

void AudioEngine::setVolume(float v) {
    m_volume = std::max(0.0f, std::min(1.0f, v));
}

void AudioEngine::attachHitsounds(const float* samples, int sampleCount, int sampleRate,
                                   const double* timestamps, int hitCount) {
    m_hitSamples = samples;
    m_hitSampleCount = sampleCount;
    m_hitSampleRate = sampleRate;
    m_hitTimestamps = timestamps;
    m_hitCount = hitCount;
    m_hitCursor = 0;
    m_hitMixOffset = 0;
    m_hitBaseTime = 0.0;
    m_totalFrames = 0;
}

void AudioEngine::setHitBaseTime() {
    if (m_vorbis && m_sampleRate > 0)
        m_hitBaseTime = (double)stb_vorbis_get_sample_offset(m_vorbis) / (double)m_sampleRate;
    else
        m_hitBaseTime = 0.0;
}

void AudioEngine::dataCallback(ma_device* pDevice, void* pOutput, const void*, unsigned int frameCount) {
    auto* self = static_cast<AudioEngine*>(pDevice->pUserData);
    float* out = (float*)pOutput;
    unsigned int devCh = pDevice->playback.channels;
    unsigned int total = frameCount * devCh;

    // Fill with silence first
    for (unsigned int i = 0; i < total; i++) out[i] = 0.0f;

    if (!self) return;

    // --- Music source ---
    if (self->m_vorbis && self->m_playing) {
        float* musicBuf = (float*)alloca(frameCount * (unsigned)self->m_channels * sizeof(float));
        int decoded = stb_vorbis_get_samples_float_interleaved(
            self->m_vorbis, self->m_channels, musicBuf, (int)frameCount * self->m_channels);

        if (decoded > 0) {
            // Mix into output (mono/stereo → device channels)
            for (int i = 0; i < decoded; i++) {
                for (int c = 0; c < (int)self->m_channels && c < (int)devCh; c++) {
                    out[i * devCh + c] += musicBuf[i * self->m_channels + c] * self->m_volume;
                }
            }
        }
        if (decoded == 0) self->m_playing = false;
    }

    // --- Hitsound scheduling (real-time, no pre-synthesis) ---
    if (self->m_hitSamples) {
        // Clock source: vorbis decoder if available, otherwise accumulated frame count
        double audioTime;
        if (self->m_vorbis && self->m_sampleRate > 0)
            audioTime = (double)stb_vorbis_get_sample_offset(self->m_vorbis) / (double)self->m_sampleRate;
        else
            audioTime = (double)(self->m_totalFrames) / (double)pDevice->sampleRate;
        self->m_totalFrames += frameCount;

        double relTime = audioTime - self->m_hitBaseTime;
        double chunkEnd = relTime + (double)frameCount / (double)pDevice->sampleRate;

        // Continue mixing active hit across chunk boundaries
        if (self->m_hitMixOffset > 0 && self->m_hitCursor < self->m_hitCount) {
            int remaining = self->m_hitSampleCount - self->m_hitMixOffset;
            int count = remaining < (int)frameCount ? remaining : (int)frameCount;
            for (int i = 0; i < count; i++) {
                float hv = self->m_hitSamples[self->m_hitMixOffset + i];
                out[i * devCh]     += hv;
                out[i * devCh + 1] += hv;
            }
            self->m_hitMixOffset += count;
            if (self->m_hitMixOffset >= self->m_hitSampleCount) {
                self->m_hitMixOffset = 0;
                self->m_hitCursor++;
            }
        }

        // Batch-skip fully expired hits
        double expiryCutoff = relTime - (double)self->m_hitSampleCount / (double)self->m_hitSampleRate - 0.001;
        while (self->m_hitCursor < self->m_hitCount
               && self->m_hitTimestamps[self->m_hitCursor] < expiryCutoff)
            self->m_hitCursor++;

        // Start new hits that begin in this chunk
        int batchMax = self->m_hitCursor + 512;
        while (self->m_hitCursor < self->m_hitCount
               && self->m_hitCursor < batchMax
               && self->m_hitTimestamps[self->m_hitCursor] < chunkEnd) {
            double ts = self->m_hitTimestamps[self->m_hitCursor];
            int dstStart = (int)((ts - relTime) * (double)pDevice->sampleRate);
            int srcStart = dstStart < 0 ? -dstStart : 0;
            int count = self->m_hitSampleCount - srcStart;
            int dstIdx = dstStart < 0 ? 0 : dstStart;
            if (dstIdx + count > (int)frameCount) count = (int)frameCount - dstIdx;
            for (int i = 0; i < count; i++) {
                float hv = self->m_hitSamples[srcStart + i];
                int si = (dstIdx + i) * devCh;
                out[si]     += hv;
                out[si + 1] += hv;
            }
            // If hit extends beyond chunk, track offset for next callback
            int mixed = srcStart + count;
            if (mixed < self->m_hitSampleCount) {
                self->m_hitMixOffset = mixed;
                break;
            }
            self->m_hitMixOffset = 0;
            self->m_hitCursor++;
        }
    }

}
