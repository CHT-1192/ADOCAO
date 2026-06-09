#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Orthographic camera, always follows the current pivot planet.
// View matrix is static (lookAt from origin); projection only changes on zoom/aspect.
class Camera {
public:
    Camera();

    void setZoom(float zoom);           // ADOFAI zoom (100 = default)
    void setAspect(float width, float height);
    void setTarget(double x, double y);  // only stores — no matrix recomputation

    glm::mat4 viewProj() const { return m_proj * m_view; }

    float zoom() const { return m_zoom; }
    double targetX() const { return m_targetX; }
    double targetY() const { return m_targetY; }
    void frustumBounds(float& left, float& right, float& bottom, float& top) const;

private:
    float m_zoom   = 100.0f;
    float m_aspect = 16.0f / 9.0f;
    double m_targetX = 0.0;
    double m_targetY = 0.0;
    float m_halfH = 6.0f;
    float m_halfW = 10.6667f;

    glm::mat4 m_proj = glm::mat4(1.0f);
    glm::mat4 m_view = glm::mat4(1.0f);

    void updateProj();
};
