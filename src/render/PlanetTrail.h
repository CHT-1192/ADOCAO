#pragma once

#include "../glad/gl_core.h"
#include <glm/glm.hpp>
#include <vector>

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
    void draw(Shader& shader, const Camera& camera, double camX, double camY);
    void clear();

private:
    struct Point { glm::vec2 pos; float time; };
    std::vector<Point> m_points;
    glm::vec2 m_center{0.0f};  // trail VBO is relative to this (for precision)

    int m_maxPoints = 200;
    float m_trailDuration = 0.4f;
    float m_planetRadius;
    glm::vec3 m_color;

    mutable GLuint m_vao = 0, m_vbo = 0, m_ebo = 0;
    mutable unsigned m_vertexCount = 0, m_indexCount = 0;
    mutable bool m_dirty = false;

    static glm::vec2 catmullRom(const glm::vec2& p0, const glm::vec2& p1,
                                const glm::vec2& p2, const glm::vec2& p3, float t);
    static glm::vec2 catmullRomTangent(const glm::vec2& p0, const glm::vec2& p1,
                                       const glm::vec2& p2, const glm::vec2& p3, float t);

    void rebuildGeometry();
    void ensureGPUResources() const;
};
