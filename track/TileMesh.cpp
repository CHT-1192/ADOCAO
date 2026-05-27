#include "TileMesh.h"
#include "TileGeometry.h"
#include "glad/gl_core.h"
#include "util/Logger.h"
#include <cmath>
#include <map>
#include <tuple>
#include <unordered_map>

// Geometry cache (persists across builds)
struct CachedGeo { std::vector<float> interleaved; std::vector<unsigned> indices; unsigned idxCount=0; };
static std::unordered_map<std::string, CachedGeo> s_geoCache;

// ---- TileMesh ----

TileMesh::~TileMesh() { destroy(); }

TileMesh::TileMesh(TileMesh&& o) noexcept : m_shapes(std::move(o.m_shapes)) {}
TileMesh& TileMesh::operator=(TileMesh&& o) noexcept { if(this!=&o){destroy();m_shapes=std::move(o.m_shapes);} return *this; }

void TileMesh::destroy() {
    for (auto& s : m_shapes) {
        if (s.instVbo) glDeleteBuffers(1, &s.instVbo);
        if (s.ebo) glDeleteBuffers(1, &s.ebo);
        if (s.vbo) glDeleteBuffers(1, &s.vbo);
        if (s.vao) glDeleteVertexArrays(1, &s.vao);
    }
    m_shapes.clear();
    for (auto& s : m_iconGroups) {
        if (s.instVbo) glDeleteBuffers(1, &s.instVbo);
        if (s.ebo) glDeleteBuffers(1, &s.ebo);
        if (s.vbo) glDeleteBuffers(1, &s.vbo);
        if (s.vao) glDeleteVertexArrays(1, &s.vao);
    }
    m_iconGroups.clear();
}

bool TileMesh::empty() const { return m_shapes.empty(); }

void TileMesh::build(const LevelData& level, const std::string& fillColorHex, const std::string& strokeColorHex) {
    destroy();
    const auto& tiles = level.tiles;
    if (tiles.size() < 2) return;

    int n = (int)tiles.size() - 1;  // number of real tiles (excludes extra)

    auto hexToColor = [](const std::string& hex) -> std::tuple<float,float,float> {
        unsigned v = hexToUInt(hex);
        return {((v>>16)&0xFF)/255.0f, ((v>>8)&0xFF)/255.0f, (v&0xFF)/255.0f};
    };
    auto [fillR, fillG, fillB] = hexToColor(fillColorHex);
    auto [outR, outG, outB]   = hexToColor(strokeColorHex);

    // Group tiles by shape key
    std::map<std::tuple<int,int,bool>, std::vector<int>> shapeGroups;

    for (int i = 0; i < n; i++) {
        float startAngle = (i == 0) ? -180.0f : tiles[i-1].direction - 180.0f;
        float endAngle   = tiles[i].direction;
        bool midspin = (i < (int)level.angleData.size() && level.angleData[i] == 999.0);

        auto key = std::make_tuple((int)std::round(startAngle*100), (int)std::round(endAngle*100), midspin);
        shapeGroups[key].push_back(i);
    }

    Scratch& sc = g_sc;
    m_shapes.resize(shapeGroups.size());
    size_t shapeIdx = 0;

    for (auto& [key, tileIndices] : shapeGroups) {
        auto [sa, ea, mid] = key;
        float startAngle = sa / 100.0f, endAngle = ea / 100.0f;

        // Cache key = shape params only (colors are per-instance)
        char kbuf[64];
        snprintf(kbuf, sizeof(kbuf), "tile_%d_%d_%d", sa, ea, (int)mid);
        std::string cacheKey(kbuf);

        std::vector<float> interleaved;
        std::vector<unsigned> idxCopy;
        unsigned idxCount;

        auto cit = s_geoCache.find(cacheKey);
        if (cit != s_geoCache.end()) {
            interleaved = cit->second.interleaved;
            idxCopy     = cit->second.indices;
            idxCount    = cit->second.idxCount;
        } else {
            // Generate local-space geometry for this shape (once)
            sc.clear();
            if (mid)
                createMidSpinMesh(endAngle, sc);
            else
                createTileMesh(startAngle, endAngle, sc);

            // Offset stroke Z + interleave: [x,y,z, type]
            for (size_t vi = 0; vi < sc.types.size(); vi++) {
                if (sc.types[vi] == 0.0f) sc.verts[vi * 3 + 2] -= 0.001f;
            }
            size_t vc = sc.verts.size() / 3;
            interleaved.reserve(vc * 4);
            for (size_t vi = 0; vi < vc; vi++) {
                interleaved.push_back(sc.verts[vi*3]);
                interleaved.push_back(sc.verts[vi*3+1]);
                interleaved.push_back(sc.verts[vi*3+2]);
                interleaved.push_back(sc.types[vi]);
            }

            idxCopy.assign(sc.indices.begin(), sc.indices.end());
            idxCount = (unsigned)sc.indices.size();
            s_geoCache[cacheKey] = {interleaved, idxCopy, idxCount};
        }

        // Collect instance data: [offX,offY,offZ, fillR,fillG,fillB, strokeR,strokeG,strokeB, opacity]
        std::vector<float> instData;
        std::vector<TileInstance> instances;
        instData.reserve(tileIndices.size() * 10);
        instances.reserve(tileIndices.size());

        for (int i : tileIndices) {
            double wx = tiles[i].position[0];
            double wy = tiles[i].position[1];
            float wz = 2.0f - (float)i * 0.001f;
            instData.push_back((float)wx);
            instData.push_back((float)wy);
            instData.push_back(wz);
            instData.push_back(fillR); instData.push_back(fillG); instData.push_back(fillB);
            instData.push_back(outR);  instData.push_back(outG);  instData.push_back(outB);
            instData.push_back(1.0f);  // opacity
            double minX=1e99,minY=1e99,maxX=-1e99,maxY=-1e99;
            size_t vertCount = interleaved.size() / 4;  // [x,y,z,type]
            for (size_t vi = 0; vi < vertCount; vi++) {
                double lx = (double)interleaved[vi*4] + wx;
                double ly = (double)interleaved[vi*4+1] + wy;
                if (lx<minX)minX=lx; if (lx>maxX)maxX=lx;
                if (ly<minY)minY=ly; if (ly>maxY)maxY=ly;
            }
            instances.push_back({wx, wy, wz, fillR, fillG, fillB, outR, outG, outB, 1.0f, minX, minY, maxX, maxY});
        }

        // Upload GPU buffers
        ShapeGroup& sg = m_shapes[shapeIdx];
        sg.indexCount = idxCount;
        sg.instances  = std::move(instances);

        glGenVertexArrays(1, &sg.vao);
        glBindVertexArray(sg.vao);

        // Vertex VBO: [x,y,z, type] — 4 floats per vertex
        glGenBuffers(1, &sg.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, sg.vbo);
        glBufferData(GL_ARRAY_BUFFER, interleaved.size()*sizeof(float), interleaved.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(3*sizeof(float)));

        // Element buffer
        glGenBuffers(1, &sg.ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sg.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, idxCopy.size()*sizeof(unsigned), idxCopy.data(), GL_STATIC_DRAW);

        // Instance VBO: [offX,offY,offZ, fillR,fillG,fillB, strokeR,strokeG,strokeB, opacity]
        // 10 floats per instance, STRIDE = 10*sizeof(float)
        GLsizei instStride = 10 * sizeof(float);
        glGenBuffers(1, &sg.instVbo);
        glBindBuffer(GL_ARRAY_BUFFER, sg.instVbo);
        glBufferData(GL_ARRAY_BUFFER, instData.size()*sizeof(float), instData.data(), GL_DYNAMIC_DRAW);
        // Location 2: aInstOffset (vec3 at offset 0)
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, instStride, (void*)0);
        glVertexAttribDivisor(2, 1);
        // Location 3: iColor (vec3 at offset 3*sizeof(float))
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, instStride, (void*)(3*sizeof(float)));
        glVertexAttribDivisor(3, 1);
        // Location 4: iBgColor (vec3 at offset 6*sizeof(float))
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, instStride, (void*)(6*sizeof(float)));
        glVertexAttribDivisor(4, 1);
        // Location 5: iOpacity (float at offset 9*sizeof(float))
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, instStride, (void*)(9*sizeof(float)));
        glVertexAttribDivisor(5, 1);

        glBindVertexArray(0);
        shapeIdx++;
    }

    LOG_I("Built track: %d tiles → %zu shape groups", n, m_shapes.size());

    // Init visibility cache per shape group
    m_visCaches.resize(m_shapes.size());

    buildIcons(level);
}

bool TileMesh::frustumChanged(const VisibilityCache& cache, float vl, float vr, float vb, float vt) {
    if (!cache.valid) return true;
    return std::abs((float)cache.vl - vl) > 0.5f || std::abs((float)cache.vr - vr) > 0.5f
        || std::abs((float)cache.vb - vb) > 0.5f || std::abs((float)cache.vt - vt) > 0.5f;
}

void TileMesh::draw(float viewL, float viewR, float viewB, float viewT, double camX, double camY) const {
    double margin = 20.0;
    double vl = viewL - margin, vr = viewR + margin;
    double vb = viewB - margin, vt = viewT + margin;

    for (size_t si = 0; si < m_shapes.size(); si++) {
        const auto& sg = m_shapes[si];
        auto& cache = m_visCaches[si];

        // Rebuild visible set only when frustum moves enough
        if (!cache.valid || frustumChanged(cache, (float)vl, (float)vr, (float)vb, (float)vt)) {
            cache.indices.clear();
            cache.indices.reserve(sg.instances.size());
            for (int ii = (int)sg.instances.size() - 1; ii >= 0; ii--) {
                const auto& inst = sg.instances[ii];
                if (inst.maxX < vl || inst.minX > vr || inst.maxY < vb || inst.minY > vt)
                    continue;
                cache.indices.push_back(ii);
            }
            cache.vl = vl; cache.vr = vr; cache.vb = vb; cache.vt = vt;
            cache.valid = true;
        }

        if (cache.indices.empty()) continue;

        // Recompute camera-relative offsets for visible instances: 10 floats per instance
        cache.offsets.resize(cache.indices.size() * 10);
        for (size_t i = 0; i < cache.indices.size(); i++) {
            const auto& inst = sg.instances[cache.indices[i]];
            cache.offsets[i * 10]     = (float)(inst.offX - camX);
            cache.offsets[i * 10 + 1] = (float)(inst.offY - camY);
            cache.offsets[i * 10 + 2] = inst.offZ;
            cache.offsets[i * 10 + 3] = inst.fillR;
            cache.offsets[i * 10 + 4] = inst.fillG;
            cache.offsets[i * 10 + 5] = inst.fillB;
            cache.offsets[i * 10 + 6] = inst.strokeR;
            cache.offsets[i * 10 + 7] = inst.strokeG;
            cache.offsets[i * 10 + 8] = inst.strokeB;
            cache.offsets[i * 10 + 9] = inst.opacity;
        }

        // Full re-upload (positions change per frame, colors static)
        // For now upload all 10 floats per visible instance
        glBindVertexArray(sg.vao);
        glBindBuffer(GL_ARRAY_BUFFER, sg.instVbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, cache.offsets.size()*sizeof(float), cache.offsets.data());
        glDrawElementsInstanced(GL_TRIANGLES, sg.indexCount, GL_UNSIGNED_INT,
                                nullptr, (GLsizei)(cache.indices.size()));
        glBindVertexArray(0);
    }
}

// ---- Event icons ----

static constexpr float ICON_RADIUS = 0.11f;     // matches Re_ADOJAS 0.275*0.8/2
static constexpr int   ICON_SEGMENTS = 16;
static constexpr float DECO_Z = 0.01f;         // Z offset above tile to prevent Z-fight
static constexpr float DECO_Z_EXTRA = 0.005f;  // extra Z for SetSpeed when Twirl also present

static const float TWIRL_COLOR[3]      = {0.502f, 0.0f, 0.502f};
static const float SPEED_UP_COLOR[3]   = {1.0f, 0.0f, 0.0f};
static const float SPEED_DOWN_COLOR[3] = {0.0f, 0.0f, 1.0f};

void TileMesh::buildIcons(const LevelData& level) {
    // Destroy existing icon groups
    for (auto& s : m_iconGroups) {
        if (s.instVbo) glDeleteBuffers(1, &s.instVbo);
        if (s.ebo) glDeleteBuffers(1, &s.ebo);
        if (s.vbo) glDeleteBuffers(1, &s.vbo);
        if (s.vao) glDeleteVertexArrays(1, &s.vao);
    }
    m_iconGroups.clear();

    const auto& tiles = level.tiles;
    int n = (int)tiles.size() - 1;  // exclude extra tile
    if (n <= 0) return;

    // Collect icon instances by color: 0=twirl, 1=speedUp, 2=speedDown
    struct IconInst { int tileIdx; float zOff; };
    std::vector<IconInst> colorGroups[3];

    for (int i = 0; i < n; i++) {
        bool hasTwirl = i < (int)level.tileHasTwirl.size() && level.tileHasTwirl[i];
        bool hasSetSpeed = i < (int)level.tileHasSetSpeed.size() && level.tileHasSetSpeed[i];

        float tileZ = 1.0f - (float)i / (float)n * 0.5f;  // tile 0 at Z=1.0, last at Z=0.5

        if (hasTwirl) {
            colorGroups[0].push_back({i, tileZ + DECO_Z});
        }
        if (hasSetSpeed && i > 0 && i < (int)level.tileBPMs.size()) {
            float ratio = level.tileBPMs[i] / level.tileBPMs[i - 1];
            if (ratio > 1.05f || ratio < 0.95f) {
                int cg = (ratio > 1.05f) ? 1 : 2;
                float extraZ = hasTwirl ? DECO_Z_EXTRA : 0.0f;
                colorGroups[cg].push_back({i, tileZ + DECO_Z + extraZ});
            }
        }
    }

    const float* colors[3] = {TWIRL_COLOR, SPEED_UP_COLOR, SPEED_DOWN_COLOR};
    Scratch& sc = g_sc;

    for (int cg = 0; cg < 3; cg++) {
        if (colorGroups[cg].empty()) continue;

        auto& grp = colorGroups[cg];
        float cr = colors[cg][0], cgv = colors[cg][1], cb = colors[cg][2];

        // Generate circle geometry with this color
        sc.clear();
        createCircle(0.0f, 0.0f, ICON_RADIUS, 1.0f, sc, ICON_SEGMENTS);

        // Interleave: [x,y,z, type] — icons always type=1.0 (fill only)
        size_t vc = sc.verts.size() / 3;
        std::vector<float> interleaved;
        interleaved.reserve(vc * 4);
        for (size_t vi = 0; vi < vc; vi++) {
            interleaved.push_back(sc.verts[vi*3]);
            interleaved.push_back(sc.verts[vi*3+1]);
            interleaved.push_back(sc.verts[vi*3+2]);
            interleaved.push_back(sc.types[vi]);
        }

        // Instance data: [offX,offY,offZ, fillR,fillG,fillB, strokeR,strokeG,strokeB, opacity]
        std::vector<float> instData;
        std::vector<TileInstance> instances;
        instData.reserve(grp.size() * 10);
        instances.reserve(grp.size());

        for (auto& icon : grp) {
            double wx = tiles[icon.tileIdx].position[0];
            double wy = tiles[icon.tileIdx].position[1];
            float wz = icon.zOff;
            instData.push_back((float)wx);
            instData.push_back((float)wy);
            instData.push_back(wz);
            instData.push_back(cr); instData.push_back(cgv); instData.push_back(cb);  // fill = icon color
            instData.push_back(cr); instData.push_back(cgv); instData.push_back(cb);  // stroke = same
            instData.push_back(1.0f);  // opacity
            instances.push_back({wx, wy, wz, cr, cgv, cb, cr, cgv, cb, 1.0f,
                wx - ICON_RADIUS, wy - ICON_RADIUS,
                wx + ICON_RADIUS, wy + ICON_RADIUS});
        }

        // Upload GPU buffers
        ShapeGroup sg;
        sg.indexCount = (unsigned)sc.indices.size();
        sg.instances = std::move(instances);

        glGenVertexArrays(1, &sg.vao);
        glBindVertexArray(sg.vao);

        glGenBuffers(1, &sg.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, sg.vbo);
        glBufferData(GL_ARRAY_BUFFER, interleaved.size()*sizeof(float), interleaved.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(3*sizeof(float)));

        glGenBuffers(1, &sg.ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sg.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sc.indices.size()*sizeof(unsigned), sc.indices.data(), GL_STATIC_DRAW);

        GLsizei istr = 10 * sizeof(float);
        glGenBuffers(1, &sg.instVbo);
        glBindBuffer(GL_ARRAY_BUFFER, sg.instVbo);
        glBufferData(GL_ARRAY_BUFFER, instData.size()*sizeof(float), instData.data(), GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, istr, (void*)0);
        glVertexAttribDivisor(2, 1);
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, istr, (void*)(3*sizeof(float)));
        glVertexAttribDivisor(3, 1);
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, istr, (void*)(6*sizeof(float)));
        glVertexAttribDivisor(4, 1);
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, istr, (void*)(9*sizeof(float)));
        glVertexAttribDivisor(5, 1);

        glBindVertexArray(0);

        m_iconGroups.push_back(std::move(sg));
    }

    LOG_I("Built event icons: %zu icon groups", m_iconGroups.size());
}

void TileMesh::drawIcons(float viewL, float viewR, float viewB, float viewT, double camX, double camY) const {
    double margin = 20.0;
    double vl = viewL - margin, vr = viewR + margin;
    double vb = viewB - margin, vt = viewT + margin;

    for (const auto& sg : m_iconGroups) {
        std::vector<float> visOffsets;
        visOffsets.reserve(sg.instances.size() * 3);
        for (int ii = (int)sg.instances.size() - 1; ii >= 0; ii--) {
            const auto& inst = sg.instances[ii];
            if (inst.maxX < vl || inst.minX > vr || inst.maxY < vb || inst.minY > vt)
                continue;
            visOffsets.push_back((float)(inst.offX - camX));
            visOffsets.push_back((float)(inst.offY - camY));
            visOffsets.push_back(inst.offZ);
        }
        if (visOffsets.empty()) continue;

        glBindVertexArray(sg.vao);
        glBindBuffer(GL_ARRAY_BUFFER, sg.instVbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, visOffsets.size()*sizeof(float), visOffsets.data());
        glDrawElementsInstanced(GL_TRIANGLES, sg.indexCount, GL_UNSIGNED_INT,
                                nullptr, (GLsizei)(visOffsets.size() / 3));
        glBindVertexArray(0);
    }
}

unsigned int TileMesh::hexToUInt(const std::string& hex) {
    unsigned v = 0;
    for (char c : hex) { v<<=4;
        if (c>='0'&&c<='9')v|=c-'0';
        else if (c>='a'&&c<='f')v|=c-'a'+10;
        else if (c>='A'&&c<='F')v|=c-'A'+10;
        else break; }
    return v;
}
