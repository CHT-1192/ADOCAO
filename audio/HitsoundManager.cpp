#include "HitsoundManager.h"
#include "util/Logger.h"

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <unordered_map>
#include <thread>
#include <future>

static std::unordered_map<std::string, std::vector<float>> s_wavCache;

#ifdef _WIN32
#include <windows.h>
#endif

static const char* hitsoundKey(const std::string& type) {
    if (type == "Kick")              return "Kick.wav";
    if (type == "KickHouse")         return "KickHouse.wav";
    if (type == "KickChroma")        return "KickChroma.wav";
    if (type == "KickRupture")       return "KickRupture.wav";
    if (type == "Snare")             return "SnareAcoustic2.wav";
    if (type == "SnareHouse")        return "SnareHouse.wav";
    if (type == "SnareVapor")        return "SnareVapor.wav";
    if (type == "Clap")              return "ClapHit.wav";
    if (type == "ClapHit")           return "ClapHit.wav";
    if (type == "ClapHitEcho")       return "ClapHitEcho.wav";
    if (type == "Hat")               return "Hat.wav";
    if (type == "HatHouse")          return "HatHouse.wav";
    if (type == "Chuck")             return "Chuck.wav";
    if (type == "Hammer")            return "Hammer.wav";
    if (type == "Shaker")            return "Shaker.wav";
    if (type == "ShakerLoud")        return "ShakerLoud.wav";
    if (type == "Sidestick")         return "Sidestick.wav";
    if (type == "Stick")             return "Stick.wav";
    if (type == "ReverbClack")       return "ReverbClack.wav";
    if (type == "ReverbClap")        return "ReverbClap.wav";
    if (type == "Squareshot")        return "Squareshot.wav";
    if (type == "FireTile")          return "FireTile.wav";
    if (type == "IceTile")           return "IceTile.wav";
    if (type == "PowerUp")           return "PowerUp.wav";
    if (type == "PowerDown")         return "PowerDown.wav";
    if (type == "VehiclePositive")   return "VehiclePositive.wav";
    if (type == "VehicleNegative")   return "VehicleNegative.wav";
    if (type == "Sizzle")            return "Sizzle.wav";
    return nullptr;
}

static std::string findAssetsDir() {
#ifdef _WIN32
    char exePath[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        std::string dir(exePath, len);
        auto pos = dir.find_last_of("\\/");
        if (pos != std::string::npos) dir = dir.substr(0, pos);
        return dir + "/assets/sounds/";
    }
#endif
    return "assets/sounds/";
}

HitsoundManager::HitsoundManager() = default;
HitsoundManager::~HitsoundManager() { m_buffer.clear(); }

void HitsoundManager::init(const std::string& assetsDir) {
    m_assetsDir = assetsDir.empty() ? findAssetsDir() : assetsDir;
}

std::string HitsoundManager::hitsoundPath(const std::string& type) const {
    const char* fn = hitsoundKey(type);
    return fn ? (m_assetsDir + fn) : std::string();
}

void HitsoundManager::setHitsoundType(const std::string& type) {
    if (m_hitsoundType == type) return;
    m_hitsoundType = type;
    m_synthesized = false;
}

void HitsoundManager::setVolume(float vol) {
    m_volume = std::max(0.0f, std::min(100.0f, vol)) / 100.0f;
}

void HitsoundManager::setEnabled(bool enabled) {
    m_enabled = enabled;
    if (!enabled) stop();
}

bool HitsoundManager::readWav(const std::string& filepath,
                               std::vector<float>& samples,
                               int& sampleRate, int& channels) {
    // Check cache first
    auto it = s_wavCache.find(filepath);
    if (it != s_wavCache.end()) {
        sampleRate = 44100; channels = 1;  // cached data is always mono 44100
        samples = it->second;
        return true;
    }

    FILE* f = fopen(filepath.c_str(), "rb");
    if (!f) { LOG_E("Hitsound: Cannot open %s", filepath.c_str()); return false; }

    char riff[4]; uint32_t fs; char wave[4];
    fread(riff,1,4,f); fread(&fs,4,1,f); fread(wave,1,4,f);
    if (memcmp(riff,"RIFF",4) || memcmp(wave,"WAVE",4)) { fclose(f); return false; }

    uint16_t bits=0, nch=0; uint32_t sr=0, dsize=0; long doff=0;
    while (!feof(f)) {
        char id[4]; uint32_t cs;
        if (fread(id,1,4,f)!=4) break;
        if (fread(&cs,4,1,f)!=1) break;
        if (!memcmp(id,"fmt ",4) && cs>=16) {
            uint16_t af; fread(&af,2,1,f); fread(&nch,2,1,f);
            fread(&sr,4,1,f); fseek(f,6,SEEK_CUR); fread(&bits,2,1,f);
            if (cs>16) fseek(f, cs-16, SEEK_CUR);
        } else if (!memcmp(id,"data",4)) {
            dsize=cs; doff=ftell(f); fseek(f,cs,SEEK_CUR);
        } else fseek(f,cs,SEEK_CUR);
    }
    if (bits!=16 || dsize==0) { fclose(f); return false; }

    sampleRate=(int)sr; channels=(int)nch;
    int nf=(int)dsize/((int)bits/8)/channels;
    std::vector<int16_t> raw((size_t)nf*channels);
    fseek(f,doff,SEEK_SET); fread(raw.data(),sizeof(int16_t),raw.size(),f);
    fclose(f);

    samples.resize(raw.size());
    for (size_t i=0;i<raw.size();i++) samples[i]=(float)raw[i]/32768.0f;
    s_wavCache[filepath] = samples;  // cache for later reuse
    return true;
}

bool HitsoundManager::preSynthesize(const std::vector<float>& timestamps,
                                     float totalDuration,
                                     HitsoundProgressCb onProgress) {
    if (!m_enabled) return false;
    if (m_hitsoundType == "None" || m_hitsoundType.empty()) {
        LOG_I("Hitsound: Disabled (type=%s)", m_hitsoundType.c_str());
        return false;
    }
    std::string hp = hitsoundPath(m_hitsoundType);
    if (hp.empty()) {
        LOG_E("Hitsound: Unknown type '%s', no WAV mapping", m_hitsoundType.c_str());
        return false;
    }

    std::vector<float> hitSamples;
    int sr=0, ch=0;
    if (!readWav(hp, hitSamples, sr, ch)) {
        LOG_E("Hitsound: Failed to read WAV: %s", hp.c_str());
        return false;
    }
    if (onProgress) onProgress(5.0f);

    m_sampleRate = sr;
    int hitLenFrames = (int)hitSamples.size()/ch;
    float pad = hitLenFrames>0 ? (float)hitLenFrames/(float)sr+1.0f : 2.0f;
    int totalFrames = (int)((totalDuration+pad)*(float)sr);

    std::vector<float> sorted=timestamps;
    std::sort(sorted.begin(),sorted.end());
    int totalHits=(int)sorted.size();

    // Parallel mixing: each thread processes a chunk with per-sample softClip
    unsigned numThreads = std::max(1u, std::thread::hardware_concurrency());
    numThreads = std::min(numThreads, (unsigned)(totalHits / 500 + 1));
    size_t bufSize = (size_t)totalFrames * 2;
    int chunkSize = (totalHits + (int)numThreads - 1) / (int)numThreads;

    std::vector<std::future<std::vector<float>>> futures;
    for (unsigned t = 0; t < numThreads; t++) {
        int start = (int)t * chunkSize;
        int end = std::min(start + chunkSize, totalHits);
        if (start >= end) continue;

        futures.push_back(std::async(std::launch::async, [&, start, end]() {
            std::vector<float> localBuf(bufSize, 0.0f);
            for (int idx = start; idx < end; idx++) {
                float ts = sorted[idx];
                if (ts < 0.0f) continue;
                int sf = (int)(ts * (float)sr);
                int cl = hitLenFrames;
                if (sf + cl > totalFrames) cl = totalFrames - sf;
                if (cl <= 0) continue;
                // 16-bit integer hard-clip (matches original HitSoundGenerator.exe)
                for (int i = 0; i < cl; i++) {
                    float hv = hitSamples[(size_t)i * (size_t)ch];
                    size_t pos = (size_t)(sf + i) * 2;
                    int curL = (int)(localBuf[pos]     * 32767.0f);
                    int curR = (int)(localBuf[pos + 1] * 32767.0f);
                    int addV = (int)(hv * 32767.0f);
                    int sumL = curL + addV; if (sumL > 32767) sumL = 32767; else if (sumL < -32768) sumL = -32768;
                    int sumR = curR + addV; if (sumR > 32767) sumR = 32767; else if (sumR < -32768) sumR = -32768;
                    localBuf[pos]     = (float)sumL / 32768.0f;
                    localBuf[pos + 1] = (float)sumR / 32768.0f;
                }
            }
            return localBuf;
        }));
    }

    // Merge thread results (pure addition — hard-clip was already applied per-sample)
    m_buffer.assign(bufSize, 0.0f);
    for (auto& fut : futures) {
        auto localBuf = fut.get();
        for (size_t i = 0; i < bufSize; i++)
            m_buffer[i] = m_buffer[i] + localBuf[i];
    }

    if (onProgress) onProgress(100.0f);
    LOG_I("Hitsound: Synthesized %d hits into %.1fs buffer", totalHits, totalDuration);
    m_synthesized=true;
    return true;
}

void HitsoundManager::reset() {
    m_readCursor = 0;
    m_playing = true;
}

void HitsoundManager::stop() {
    m_playing = false;
}
