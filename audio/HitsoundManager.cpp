#include "HitsoundManager.h"
#include "util/Logger.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <unordered_map>

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

static std::unordered_map<std::string, std::vector<float>> s_wavCache;

// ---- HitsoundManager ----

void HitsoundManager::init(const std::string& assetsDir) {
    m_assetsDir = assetsDir.empty() ? findAssetsDir() : assetsDir;
}

std::string HitsoundManager::hitsoundPath(const std::string& type) const {
    const char* fn = hitsoundKey(type);
    return fn ? (m_assetsDir + fn) : std::string();
}

void HitsoundManager::setHitsoundType(const std::string& type) {
    m_hitsoundType = type;
}

bool HitsoundManager::readWav(const std::string& filepath,
                               std::vector<float>& samples, int& sampleRate, int& channels) {
    auto it = s_wavCache.find(filepath);
    if (it != s_wavCache.end()) {
        samples = it->second;
        sampleRate = 44100; channels = 1;
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
    s_wavCache[filepath] = samples;
    return true;
}

bool HitsoundManager::prepare(const std::vector<double>& timestamps) {
    if (m_hitsoundType == "None" || m_hitsoundType.empty()) return false;

    std::string hp = hitsoundPath(m_hitsoundType);
    if (hp.empty()) {
        LOG_E("Hitsound: Unknown type '%s'", m_hitsoundType.c_str());
        return false;
    }

    int ch=0;
    if (!readWav(hp, m_samples, m_sampleRate, ch)) return false;
    m_sampleCount = (int)m_samples.size() / ch;

    m_timestamps = timestamps;
    std::sort(m_timestamps.begin(), m_timestamps.end());

    LOG_I("Hitsound: Prepared %s (%d samples, %zu hits)",
          m_hitsoundType.c_str(), m_sampleCount, m_timestamps.size());
    return true;
}
