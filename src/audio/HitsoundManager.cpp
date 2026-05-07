#include "HitsoundManager.h"
#include "../util/Logger.h"

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <unordered_map>

static std::unordered_map<std::string, std::vector<float>> s_wavCache;

#ifdef _WIN32
#include <windows.h>
#endif

static const char* hitsoundKey(const std::string& type) {
    if (type == "Kick")              return "sndKick.wav";
    if (type == "KickHouse")         return "sndKickHouse.wav";
    if (type == "KickChroma")        return "sndKickChroma.wav";
    if (type == "KickRupture")       return "sndKickRupture.wav";
    if (type == "Snare")             return "sndSnareAcoustic2.wav";
    if (type == "SnareHouse")        return "sndSnareHouse.wav";
    if (type == "SnareVapor")        return "sndSnareVapor.wav";
    if (type == "Clap")              return "sndClapHit.wav";
    if (type == "ClapHit")           return "sndClapHit.wav";
    if (type == "ClapHitEcho")       return "sndClapHitEcho.wav";
    if (type == "Hat")               return "sndHat.wav";
    if (type == "HatHouse")          return "sndHatHouse.wav";
    if (type == "Chuck")             return "sndChuck.wav";
    if (type == "Hammer")            return "sndHammer.wav";
    if (type == "Shaker")            return "sndShaker.wav";
    if (type == "ShakerLoud")        return "sndShakerLoud.wav";
    if (type == "Sidestick")         return "sndSidestick.wav";
    if (type == "Stick")             return "sndStick.wav";
    if (type == "ReverbClack")       return "sndReverbClack.wav";
    if (type == "ReverbClap")        return "sndReverbClap.wav";
    if (type == "Squareshot")        return "sndSquareshot.wav";
    if (type == "FireTile")          return "sndFireTile.wav";
    if (type == "IceTile")           return "sndIceTile.wav";
    if (type == "PowerUp")           return "sndPowerUp.wav";
    if (type == "PowerDown")         return "sndPowerDown.wav";
    if (type == "VehiclePositive")   return "sndVehiclePositive.wav";
    if (type == "VehicleNegative")   return "sndVehicleNegative.wav";
    if (type == "Sizzle")            return "sndSizzle.wav";
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

static inline float softClip(float x) {
    float a=x<0?-x:x;
    if (a<0.5f) return x;
    if (a<1.5f) return x*(1.0f-x*x/3.0f);
    return x<0?-1.0f:1.0f;
}

bool HitsoundManager::preSynthesize(const std::vector<float>& timestamps,
                                     float totalDuration,
                                     HitsoundProgressCb onProgress) {
    if (!m_enabled || m_hitsoundType=="None") {
        if (onProgress) onProgress(100.0f);
        return false;
    }
    std::string hp = hitsoundPath(m_hitsoundType);
    if (hp.empty()) return false;

    std::vector<float> hitSamples;
    int sr=0, ch=0;
    if (!readWav(hp, hitSamples, sr, ch)) return false;
    if (onProgress) onProgress(5.0f);

    m_sampleRate = sr;
    int hitLenFrames = (int)hitSamples.size()/ch;
    float pad = hitLenFrames>0 ? (float)hitLenFrames/(float)sr+1.0f : 2.0f;
    int totalFrames = (int)((totalDuration+pad)*(float)sr);

    m_buffer.assign((size_t)totalFrames*2, 0.0f);
    std::vector<float> sorted=timestamps;
    std::sort(sorted.begin(),sorted.end());
    int totalHits=(int)sorted.size();
    float peak=0.0f;

    for (int idx=0; idx<totalHits; idx++) {
        float t=sorted[idx];
        if (t<0.0f) continue;
        int sf=(int)(t*(float)sr);
        int cl=hitLenFrames;
        if (sf+cl>totalFrames) cl=totalFrames-sf;
        if (cl<=0) continue;
        for (int i=0; i<cl; i++) {
            float hv=hitSamples[(size_t)i*(size_t)ch];
            m_buffer[(size_t)(sf+i)*2]   += hv;
            m_buffer[(size_t)(sf+i)*2+1] += hv;
            float a=hv<0?-hv:hv;
            if (a>peak) peak=a;
        }
    }

    float gain=(peak>0.9f)?0.9f/peak:1.0f;
    for (size_t i=0; i<m_buffer.size(); i++)
        m_buffer[i]=softClip(m_buffer[i]*gain);

    if (onProgress) onProgress(100.0f);
    LOG_I("Hitsound: Synthesized %d hits into %.1fs buffer", totalHits, totalDuration);
    m_synthesized=true;
    return true;
}

void HitsoundManager::reset() {
    m_readCursor = 0;
    m_playing = false;
}

void HitsoundManager::stop() {
    m_playing = false;
}
