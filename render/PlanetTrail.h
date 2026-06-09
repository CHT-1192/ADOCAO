#pragma once

#include "glad/gl_core.h"
#include <glm/glm.hpp>
#include <array>

class Shader;
class Camera;

class PlanetTrail {
public:
    PlanetTrail(const glm::vec3& color, float planetRadius);
    ~PlanetTrail();

    PlanetTrail(const PlanetTrail&) = delete;
    PlanetTrail& operator=(const PlanetTrail&) = delete;
    PlanetTrail(PlanetTrail&&) noexcept;
    PlanetTrail& operator=(PlanetTrail&&) noexcept;

    void update(const glm::vec2& pos, float currentTime);
    void setPoints(const float* xy, int count);  // batch set from flat XY array
    void draw(Shader& shader, const Camera& camera, double camX, double camY);
    void clear();
    void setPlanetRadius(float r) { m_planetRadius = r; }

private:
    static constexpr int MAX_POINTS = 200;
    static constexpr int MAX_VERTS = MAX_POINTS * 2;
    static constexpr int MAX_INDICES = (MAX_POINTS - 1) * 6;
    static constexpr int SEGS_PER_POINT = 4;

    struct Point { glm::vec2 pos; float time; };
    // Ring buffer: no heap alloc, no erase-shift
    std::array<Point, MAX_POINTS> m_ring;
    int m_head = 0;    // next write position
    int m_count = 0;   // number of valid points
    float m_lastTime = 0.0f;

    glm::vec2 m_center{0.0f};  // trail VBO is relative to this (for precision)

    float m_trailDuration = 0.4f;
    float m_planetRadius;
    glm::vec3 m_color;

    // Pre-allocated geometry buffers (avoid per-frame heap alloc)
    mutable std::array<float, MAX_VERTS * 3> m_verts;
    mutable std::array<unsigned, MAX_INDICES> m_indices;

    mutable GLuint m_vao = 0, m_vbo = 0, m_ebo = 0;
    mutable unsigned m_vertexCount = 0, m_indexCount = 0;
    mutable bool m_dirty = false;

    static glm::vec2 catmullRom(const glm::vec2& p0, const glm::vec2& p1,
                                const glm::vec2& p2, const glm::vec2& p3, float t);
    static glm::vec2 catmullRomTangent(const glm::vec2& p0, const glm::vec2& p1,
                                       const glm::vec2& p2, const glm::vec2& p3, float t);

    // Ring buffer helpers
    const Point& ringAt(int i) const { return m_ring[(m_head - m_count + i + MAX_POINTS) % MAX_POINTS]; }

    void rebuildGeometry();
    void ensureGPUResources() const;
};
