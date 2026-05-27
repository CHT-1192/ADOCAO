#pragma once

#include "glad/gl_core.h"
#include "level/LevelData.h"
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <map>
#include <tuple>

// Per-instance data (world position + bounding box for culling)
struct TileInstance {
    double offX, offY;       // world position (double for precision)
    float  offZ;
    double minX, minY, maxX, maxY;  // world-space AABB (double for culling)
};

struct ShapeGroup {
    GLuint vao = 0;
    GLuint vbo = 0;       // local-space vertex data (x,y,z, r,g,b)
    GLuint ebo = 0;
    GLuint instVbo = 0;   // per-instance vec2 offsets
    unsigned indexCount = 0;
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
               const std::string& strokeColorHex = "000000");

    void draw(float viewL, float viewR, float viewB, float viewT, double camX, double camY) const;
    void drawIcons(float viewL, float viewR, float viewB, float viewT, double camX, double camY) const;

    bool empty() const;

private:
    std::vector<ShapeGroup> m_shapes;
    std::vector<ShapeGroup> m_iconGroups;

    // Visibility cache: avoid recomputing visible set when camera hasn't moved
    struct VisibilityCache {
        std::vector<int>   indices;   // visible instance indices (rebuilt on frustum change)
        std::vector<float> offsets;   // camera-relative offsets (recomputed each frame)
        double vl=0, vr=0, vb=0, vt=0;
        bool valid = false;
    };
    mutable std::vector<VisibilityCache> m_visCaches;  // per-shape-group

    void destroy();
    void buildIcons(const LevelData& level);
    static unsigned int hexToUInt(const std::string& hex);
    static bool frustumChanged(const VisibilityCache& cache, float vl, float vr, float vb, float vt);
};
