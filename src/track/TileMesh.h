#pragma once

#include "../glad/gl_core.h"
#include "../level/LevelData.h"
#include <glm/glm.hpp>
#include <vector>
#include <string>

// A single tile mesh (quad segment connecting two consecutive tile positions)
struct TileSegment {
    glm::vec2 start;
    glm::vec2 end;
    glm::vec3 color;      // primary color
    glm::vec3 color2;     // secondary / edge color
    float     width = 0.4f;
    int       index = 0;
};

// Generates GPU buffers for track rendering
class TileMesh {
public:
    TileMesh() = default;
    ~TileMesh();

    TileMesh(const TileMesh&) = delete;
    TileMesh& operator=(const TileMesh&) = delete;
    TileMesh(TileMesh&&) noexcept;
    TileMesh& operator=(TileMesh&&) noexcept;

    // Build mesh from level data
    void build(const LevelData& level);

    // Render
    void draw() const;

    bool empty() const { return m_indexCount == 0; }

private:
    GLuint m_vao = 0;
    GLuint m_vbo = 0;
    GLsizei m_indexCount = 0;

    void destroy();

    static unsigned int hexToUInt(const std::string& hex);
};
