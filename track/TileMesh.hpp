#pragma once

#include "glad/gl_core.hpp"
#include "level/LevelData.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <tuple>

struct TileInstance {
    double offX, offY; float offZ;
    float fillR, fillG, fillB, strokeR, strokeG, strokeB, opacity;
    double minX, minY, maxX, maxY;
};

struct ShapeGroup {
    GLuint vao = 0, vbo = 0, ebo = 0, instVbo = 0, colorVbo = 0;
    unsigned indexCount = 0;
    unsigned strokeIndexCount = 0, fillIndexCount = 0, fillIndexByteOffset = 0;

    size_t instanceCount = 0;
    double* cullMinX = nullptr;
    double* cullMaxX = nullptr;
    double* cullMinY = nullptr;
    double* cullMaxY = nullptr;
    float* posX = nullptr;
    float* posY = nullptr;
    float* posZ = nullptr;

    double groupMinX = 1e99, groupMinY = 1e99;
    double groupMaxX = -1e99, groupMaxY = -1e99;

    // Legacy culling: keep AoS instance data (only used when legacyCulling=true)
    std::vector<TileInstance> instances;
};

class TileMesh {
public:
    TileMesh() = default;
    ~TileMesh();
    TileMesh(const TileMesh&) = delete;
    TileMesh& operator=(const TileMesh&) = delete;
    TileMesh(TileMesh&&) noexcept;
    TileMesh& operator=(TileMesh&&) noexcept;

    void build(const LevelData& level,
               const std::string& fillColorHex = "FFFFFF",
               const std::string& strokeColorHex = "000000",
               bool legacyCulling = false);
    void draw(float viewL, float viewR, float viewB, float viewT, double camX, double camY) const;
    void drawIcons(float viewL, float viewR, float viewB, float viewT, double camX, double camY) const;
    void drawHighlightedTile(int tileIdx, double camX, double camY) const;
    bool empty() const;

    struct VisibilityCache {
        std::vector<int> indices;
        std::vector<float> offsets;
        double vl=0, vr=0, vb=0, vt=0, prevCamX=0, prevCamY=0;
        bool valid=false, offsetsValid=false;
    };

    static bool frustumCheck(const VisibilityCache& c, float vl, float vr, float vb, float vt) {
        if (!c.valid) return true;
        return std::abs((float)c.vl-vl)>0.5f || std::abs((float)c.vr-vr)>0.5f
            || std::abs((float)c.vb-vb)>0.5f || std::abs((float)c.vt-vt)>0.5f;
    }
    static constexpr float kMaxTileZ = 9.0f, kIconZBase = 0.002f, kIconZExtra = 0.003f;
    static float tileZForIndex(int i, int n);

private:
    std::vector<ShapeGroup> m_shapes;
    std::vector<ShapeGroup> m_iconGroups;
    mutable std::vector<VisibilityCache> m_visCaches;
    mutable std::vector<VisibilityCache> m_iconVisCaches;
    std::vector<int> m_tileToShape, m_tileToInstance;

    bool m_legacyCulling = false;

    void destroy();
    void buildIcons(const LevelData& level);
    static unsigned int hexToUInt(const std::string& hex);
    static void freeSoA(ShapeGroup& sg);
    static void allocSoA(ShapeGroup& sg, size_t n);
};
