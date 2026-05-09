#include "TileMesh.h"
#include "TileGeometry.h"
#include "../glad/gl_core.h"
#include "../util/Logger.h"
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
        bool midspin = (i < (int)level.angleData.size() && level.angleData[i] == 999.0f);

        auto key = std::make_tuple((int)std::round(startAngle*100), (int)std::round(endAngle*100), midspin);
        shapeGroups[key].push_back(i);
    }

    Scratch& sc = g_sc;
    m_shapes.resize(shapeGroups.size());
    size_t shapeIdx = 0;

    for (auto& [key, tileIndices] : shapeGroups) {
        auto [sa, ea, mid] = key;
        float startAngle = sa / 100.0f, endAngle = ea / 100.0f;

        // Cache key = shape params + colors
        char kbuf[64];
        snprintf(kbuf, sizeof(kbuf), "tile_%d_%d_%d_%s_%s", sa, ea, (int)mid,
                 fillColorHex.c_str(), strokeColorHex.c_str());
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

            // Remap colors + offset outline Z
            for (size_t ci = 0, vi = 0; ci < sc.colors.size(); ci += 3, vi += 3) {
                if (sc.colors[ci] == 0.0f) {
                    sc.colors[ci]=outR; sc.colors[ci+1]=outG; sc.colors[ci+2]=outB;
                    sc.verts[vi + 2] -= 0.001f;
                } else {
                    sc.colors[ci]=fillR; sc.colors[ci+1]=fillG; sc.colors[ci+2]=fillB;
                }
            }

            // Interleave vertex position + color: [x,y,z, r,g,b, ...]
            size_t vc = sc.verts.size() / 3;
            interleaved.reserve(vc * 6);
            for (size_t vi = 0; vi < vc; vi++) {
                interleaved.push_back(sc.verts[vi*3]);
                interleaved.push_back(sc.verts[vi*3+1]);
                interleaved.push_back(sc.verts[vi*3+2]);
                interleaved.push_back(sc.colors[vi*3]);
                interleaved.push_back(sc.colors[vi*3+1]);
                interleaved.push_back(sc.colors[vi*3+2]);
            }

            idxCopy.assign(sc.indices.begin(), sc.indices.end());
            idxCount = (unsigned)sc.indices.size();
            s_geoCache[cacheKey] = {interleaved, idxCopy, idxCount};
        }

        // Collect instance data (vec3: x, y, z)
        std::vector<float> instOffsets;
        std::vector<TileInstance> instances;
        instOffsets.reserve(tileIndices.size() * 3);
        instances.reserve(tileIndices.size());

        for (int i : tileIndices) {
            float wx = tiles[i].position[0];
            float wy = tiles[i].position[1];
            float wz = 2.0f - (float)i * 0.001f;  // tile 0 Z=2.0, tile 1M Z=-998 (w/in far plane)
            instOffsets.push_back(wx);
            instOffsets.push_back(wy);
            instOffsets.push_back(wz);
            float minX=1e9f,minY=1e9f,maxX=-1e9f,maxY=-1e9f;
            size_t vertCount = interleaved.size() / 6;  // 6 floats per vertex
            for (size_t vi = 0; vi < vertCount; vi++) {
                float lx = interleaved[vi*6] + wx;
                float ly = interleaved[vi*6+1] + wy;
                if (lx<minX)minX=lx; if (lx>maxX)maxX=lx;
                if (ly<minY)minY=ly; if (ly>maxY)maxY=ly;
            }
            instances.push_back({wx, wy, wz, minX, minY, maxX, maxY});
        }

        // Upload GPU buffers
        ShapeGroup& sg = m_shapes[shapeIdx];
        sg.indexCount = idxCount;
        sg.instances  = std::move(instances);

        glGenVertexArrays(1, &sg.vao);
        glBindVertexArray(sg.vao);

        // Vertex VBO (local space, interleaved)
        glGenBuffers(1, &sg.vbo);
        glBindBuffer(GL_ARRAY_BUFFER, sg.vbo);
        glBufferData(GL_ARRAY_BUFFER, interleaved.size()*sizeof(float), interleaved.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));

        // Element buffer
        glGenBuffers(1, &sg.ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sg.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, idxCopy.size()*sizeof(unsigned), idxCopy.data(), GL_STATIC_DRAW);

        // Instance VBO (vec2 offsets, dynamic for visibility filtering)
        glGenBuffers(1, &sg.instVbo);
        glBindBuffer(GL_ARRAY_BUFFER, sg.instVbo);
        glBufferData(GL_ARRAY_BUFFER, instOffsets.size()*sizeof(float), instOffsets.data(), GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
        glVertexAttribDivisor(2, 1); // per-instance

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
    return std::abs(cache.viewL - vl) > 0.5f || std::abs(cache.viewR - vr) > 0.5f
        || std::abs(cache.viewB - vb) > 0.5f || std::abs(cache.viewT - vt) > 0.5f;
}

void TileMesh::draw(float viewL, float viewR, float viewB, float viewT) const {
    float margin = 20.0f;
    float vl = viewL - margin, vr = viewR + margin;
    float vb = viewB - margin, vt = viewT + margin;

    for (size_t si = 0; si < m_shapes.size(); si++) {
        const auto& sg = m_shapes[si];
        auto& cache = m_visCaches[si];

        // Use cached visibility when camera is nearly stationary
        if (!frustumChanged(cache, vl, vr, vb, vt) && !cache.offsets.empty()) {
            glBindVertexArray(sg.vao);
            glBindBuffer(GL_ARRAY_BUFFER, sg.instVbo);
            glBufferSubData(GL_ARRAY_BUFFER, 0, cache.offsets.size()*sizeof(float), cache.offsets.data());
            glDrawElementsInstanced(GL_TRIANGLES, sg.indexCount, GL_UNSIGNED_INT,
                                    nullptr, (GLsizei)(cache.offsets.size() / 3));
            glBindVertexArray(0);
            continue;
        }

        // Recompute visible set (reverse order: later tiles first/back, earlier last/front)
        cache.offsets.clear();
        cache.offsets.reserve(sg.instances.size() * 3);
        for (int ii = (int)sg.instances.size() - 1; ii >= 0; ii--) {
            const auto& inst = sg.instances[ii];
            if (inst.maxX < vl || inst.minX > vr || inst.maxY < vb || inst.minY > vt)
                continue;
            cache.offsets.push_back(inst.offX);
            cache.offsets.push_back(inst.offY);
            cache.offsets.push_back(inst.offZ);
        }
        cache.viewL = vl; cache.viewR = vr; cache.viewB = vb; cache.viewT = vt;
        cache.valid = true;

        if (cache.offsets.empty()) continue;

        glBindVertexArray(sg.vao);
        glBindBuffer(GL_ARRAY_BUFFER, sg.instVbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, cache.offsets.size()*sizeof(float), cache.offsets.data());

        glDrawElementsInstanced(GL_TRIANGLES, sg.indexCount, GL_UNSIGNED_INT,
                                nullptr, (GLsizei)(cache.offsets.size() / 3));

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
        createCircle(0.0f, 0.0f, ICON_RADIUS, cr, cgv, cb, sc, ICON_SEGMENTS);

        // Interleave vertex data: [x,y,z, r,g,b, ...]
        size_t vc = sc.verts.size() / 3;
        std::vector<float> interleaved;
        interleaved.reserve(vc * 6);
        for (size_t vi = 0; vi < vc; vi++) {
            interleaved.push_back(sc.verts[vi*3]);
            interleaved.push_back(sc.verts[vi*3+1]);
            interleaved.push_back(sc.verts[vi*3+2]);
            interleaved.push_back(sc.colors[vi*3]);
            interleaved.push_back(sc.colors[vi*3+1]);
            interleaved.push_back(sc.colors[vi*3+2]);
        }

        // Instance offsets + AABBs
        std::vector<float> instOffsets;
        std::vector<TileInstance> instances;
        instOffsets.reserve(grp.size() * 3);
        instances.reserve(grp.size());

        for (auto& icon : grp) {
            float wx = tiles[icon.tileIdx].position[0];
            float wy = tiles[icon.tileIdx].position[1];
            float wz = icon.zOff;
            instOffsets.push_back(wx);
            instOffsets.push_back(wy);
            instOffsets.push_back(wz);
            instances.push_back({wx, wy, wz,
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
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));

        glGenBuffers(1, &sg.ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sg.ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sc.indices.size()*sizeof(unsigned), sc.indices.data(), GL_STATIC_DRAW);

        glGenBuffers(1, &sg.instVbo);
        glBindBuffer(GL_ARRAY_BUFFER, sg.instVbo);
        glBufferData(GL_ARRAY_BUFFER, instOffsets.size()*sizeof(float), instOffsets.data(), GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
        glVertexAttribDivisor(2, 1);

        glBindVertexArray(0);

        m_iconGroups.push_back(std::move(sg));
    }

    LOG_I("Built event icons: %zu icon groups", m_iconGroups.size());
}

void TileMesh::drawIcons(float viewL, float viewR, float viewB, float viewT) const {
    float margin = 20.0f;
    float vl = viewL - margin, vr = viewR + margin;
    float vb = viewB - margin, vt = viewT + margin;

    for (const auto& sg : m_iconGroups) {
        std::vector<float> visOffsets;
        visOffsets.reserve(sg.instances.size() * 3);

        for (int ii = (int)sg.instances.size() - 1; ii >= 0; ii--) {
            const auto& inst = sg.instances[ii];
            if (inst.maxX < vl || inst.minX > vr || inst.maxY < vb || inst.minY > vt)
                continue;
            visOffsets.push_back(inst.offX);
            visOffsets.push_back(inst.offY);
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
