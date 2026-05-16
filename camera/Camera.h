#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Orthographic camera, always follows the current pivot planet.
// Fixed relativeTo=Player, position=(0,0).
class Camera {
public:
    Camera() = default;

    void setZoom(float zoom);           // ADOFAI zoom (100 = default)
    void setAspect(float width, float height);
    void setTarget(double x, double y);

    glm::mat4 viewProj() const;
    glm::mat4 proj()    const { return m_proj; }
    glm::mat4 view()    const { return m_view; }

    float zoom() const { return m_zoom; }
    double targetX() const { return m_targetX; }
    double targetY() const { return m_targetY; }
    void frustumBounds(float& left, float& right, float& bottom, float& top) const;

private:
    float m_zoom   = 100.0f;
    float m_aspect = 16.0f / 9.0f;
    double m_targetX = 0.0;
    double m_targetY = 0.0;

    glm::mat4 m_proj = glm::mat4(1.0f);
    glm::mat4 m_view = glm::mat4(1.0f);

    void update();
};
