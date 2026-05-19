#pragma once

#include "glad/gl_core.h"
#include "level/LevelData.h"
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <map>
#include <tuple>

struct TileInstance {
    double offX, offY;
    float  offZ;
    double minX, minY, maxX, maxY;  // world-space AABB (double for culling)
};

struct ShapeGroup {
    GLuint vao = 0;
    GLuint vbo = 0;       // local-space vertex data (x,y,z, r,g,b)
    GLuint ebo = 0;
    GLuint instVbo = 0;   // per-instance vec3 world offsets (uploaded once)
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

    void destroy();
    void buildIcons(const LevelData& level);
    static unsigned int hexToUInt(const std::string& hex);
};
