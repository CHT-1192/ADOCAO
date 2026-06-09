#include "TileMesh.h"
#include "TileGeometry.h"
#include "glad/gl_core.h"
#include "util/Logger.h"
#include <GLFW/glfw3.h>
#include <cmath>
#include <map>
#include <tuple>
#include <unordered_map>

// OpenGL 4.2 function (loaded manually — glad targets 3.3)
static void (GLAPIENTRY *glDrawElementsInstancedBaseInstance)(GLenum, GLsizei, GLenum, const void*, GLsizei, GLuint) = nullptr;

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
        if (s.colorVbo) glDeleteBuffers(1, &s.colorVbo);
        if (s.ebo) glDeleteBuffers(1, &s.ebo);
        if (s.vbo) glDeleteBuffers(1, &s.vbo);
        if (s.vao) glDeleteVertexArrays(1, &s.vao);
    }
    m_shapes.clear();
    for (auto& s : m_iconGroups) {
        if (s.instVbo) glDeleteBuffers(1, &s.instVbo);
        if (s.colorVbo) glDeleteBuffers(1, &s.colorVbo);
        if (s.ebo) glDeleteBuffers(1, &s.ebo);
        if (s.vbo) glDeleteBuffers(1, &s.vbo);
        if (s.vao) glDeleteVertexArrays(1, &s.vao);
    }
    m_iconGroups.clear();
}

bool TileMesh::empty() const { return m_shapes.empty(); }

void TileMesh::build(const LevelData& level, const std::string& fillColorHex, const std::string& strokeColorHex) {
    destroy();

    // Load GL 4.2 function (available on most GPUs since 2011)
    if (!glDrawElementsInstancedBaseInstance)
        glDrawElementsInstancedBaseInstance = (decltype(glDrawElementsInstancedBaseInstance))
            glfwGetProcAddress("glDrawElementsInstancedBaseInstance");

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
        // Sort descending: high index first = drawn first (Re_ADOJAS renderOrder = -tileIndex)
        std::sort(tileIndices.begin(), tileIndices.end(), std::greater<int>());
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
                if (sc.types[vi] == 0.0f) sc.verts[vi * 3 + 2] += 0.001f;
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
            float wz = 0.0f;  // constant Z — draw order (descending) handles layering
            // Per-tile color from ColorTrack events, fallback to global
            float fr = fillR, fg = fillG, fb = fillB;
            float sr = outR, sg = outG, sb = outB;
            if (i < (int)level.tileFillColors.size() && !level.tileFillColors[i].empty()) {
                unsigned fv = hexToUInt(level.tileFillColors[i]);
                fr = ((fv>>16)&0xFF)/255.0f; fg = ((fv>>8)&0xFF)/255.0f; fb = (fv&0xFF)/255.0f;
            }
            if (i < (int)level.tileStrokeColors.size() && !level.tileStrokeColors[i].empty()) {
                unsigned sv = hexToUInt(level.tileStrokeColors[i]);
                sr = ((sv>>16)&0xFF)/255.0f; sg = ((sv>>8)&0xFF)/255.0f; sb = (sv&0xFF)/255.0f;
            }
            instData.push_back((float)wx);
            instData.push_back((float)wy);
            instData.push_back(wz);
            instData.push_back(fr); instData.push_back(fg); instData.push_back(fb);
            instData.push_back(sr); instData.push_back(sg); instData.push_back(sb);
            instData.push_back(1.0f);
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

        // Split instance data: positions (3 floats, updated per-frame) vs colors (7 floats, static)
        // Build position-only array for instVbo
        std::vector<float> posOnly;
        posOnly.reserve(tileIndices.size() * 3);
        std::vector<float> colorOnly;
        colorOnly.reserve(tileIndices.size() * 7);
        for (int i = 0; i < (int)tileIndices.size(); i++) {
            posOnly.push_back(instData[i*10]);
            posOnly.push_back(instData[i*10+1]);
            posOnly.push_back(instData[i*10+2]);
            colorOnly.push_back(instData[i*10+3]); // fillR
            colorOnly.push_back(instData[i*10+4]); // fillG
            colorOnly.push_back(instData[i*10+5]); // fillB
            colorOnly.push_back(instData[i*10+6]); // strokeR
            colorOnly.push_back(instData[i*10+7]); // strokeG
            colorOnly.push_back(instData[i*10+8]); // strokeB
            colorOnly.push_back(instData[i*10+9]); // opacity
        }

        // Instance pos VBO (location 2): vec3 offsets, updated per-frame
        glGenBuffers(1, &sg.instVbo);
        glBindBuffer(GL_ARRAY_BUFFER, sg.instVbo);
        glBufferData(GL_ARRAY_BUFFER, posOnly.size()*sizeof(float), posOnly.data(), GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
        glVertexAttribDivisor(2, 1);

        // Instance color VBO (locations 3-5): colors, static
        GLsizei cStride = 7 * sizeof(float);
        glGenBuffers(1, &sg.colorVbo);
        glBindBuffer(GL_ARRAY_BUFFER, sg.colorVbo);
        glBufferData(GL_ARRAY_BUFFER, colorOnly.size()*sizeof(float), colorOnly.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, cStride, (void*)0);
        glVertexAttribDivisor(3, 1);
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, cStride, (void*)(3*sizeof(float)));
        glVertexAttribDivisor(4, 1);
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, cStride, (void*)(6*sizeof(float)));
        glVertexAttribDivisor(5, 1);

        glBindVertexArray(0);

        shapeIdx++;
    }

    LOG_I("Built track: %d tiles → %zu shape groups", n, m_shapes.size());

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

        // Recompute camera-relative offsets: 3 floats per visible instance (position only)
        cache.offsets.resize(cache.indices.size() * 3);
        for (size_t i = 0; i < cache.indices.size(); i++) {
            const auto& inst = sg.instances[cache.indices[i]];
            cache.offsets[i * 3]     = (float)(inst.offX - camX);
            cache.offsets[i * 3 + 1] = (float)(inst.offY - camY);
            cache.offsets[i * 3 + 2] = inst.offZ;
        }

        // Upload position VBO only (colors are static in colorVbo)
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

        // Position VBO (3 floats)
        std::vector<float> ipos; ipos.reserve(grp.size() * 3);
        std::vector<float> icols; icols.reserve(grp.size() * 7);
        for (int i = 0; i < (int)grp.size(); i++) {
            ipos.push_back(instData[i*10]); ipos.push_back(instData[i*10+1]); ipos.push_back(instData[i*10+2]);
            for (int j = 3; j < 10; j++) icols.push_back(instData[i*10+j]);
        }
        glGenBuffers(1, &sg.instVbo);
        glBindBuffer(GL_ARRAY_BUFFER, sg.instVbo);
        glBufferData(GL_ARRAY_BUFFER, ipos.size()*sizeof(float), ipos.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 3*sizeof(float), (void*)0);
        glVertexAttribDivisor(2, 1);

        GLsizei cstr = 7 * sizeof(float);
        glGenBuffers(1, &sg.colorVbo);
        glBindBuffer(GL_ARRAY_BUFFER, sg.colorVbo);
        glBufferData(GL_ARRAY_BUFFER, icols.size()*sizeof(float), icols.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, cstr, (void*)0);
        glVertexAttribDivisor(3, 1);
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, cstr, (void*)(3*sizeof(float)));
        glVertexAttribDivisor(4, 1);
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 1, GL_FLOAT, GL_FALSE, cstr, (void*)(6*sizeof(float)));
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
    bool useBase = (glDrawElementsInstancedBaseInstance != nullptr);

    for (const auto& sg : m_iconGroups) {
        if (sg.instances.empty()) continue;

        glBindVertexArray(sg.vao);
        int rangeStart = -1, rangeEnd = -1;
        for (int i = 0; i < (int)sg.instances.size(); i++) {
            const auto& inst = sg.instances[i];
            bool vis = !(inst.maxX < vl || inst.minX > vr || inst.maxY < vb || inst.minY > vt);
            if (vis) {
                if (rangeStart < 0) rangeStart = i;
                rangeEnd = i + 1;
            }
            if ((!vis || i == (int)sg.instances.size() - 1) && rangeStart >= 0) {
                int count = rangeEnd - rangeStart;
                if (useBase) {
                    glDrawElementsInstancedBaseInstance(GL_TRIANGLES, sg.indexCount,
                        GL_UNSIGNED_INT, nullptr, count, (GLuint)rangeStart);
                } else {
                    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE,
                        3*sizeof(float), (void*)(rangeStart * 3 * sizeof(float)));
                    glDrawElementsInstanced(GL_TRIANGLES, sg.indexCount, GL_UNSIGNED_INT,
                                            nullptr, count);
                }
                rangeStart = -1;
            }
        }
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
