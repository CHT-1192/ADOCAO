#include "Camera.h"

void Camera::setZoom(float zoom) {
    m_zoom = zoom;
    update();
}

void Camera::setAspect(float width, float height) {
    m_aspect = width / height;
    update();
}

void Camera::setTarget(float x, float y) {
    m_targetX = x;
    m_targetY = y;
    update();
}

void Camera::update() {
    // Ortho height in world units: smaller zoom = zoomed in
    // ADOFAI zoom 100 = ~6 units visible height (fits typical level)
    float halfH = 6.0f / (m_zoom / 100.0f);
    float halfW = halfH * m_aspect;

    m_proj = glm::ortho(-halfW, halfW, -halfH, halfH, 0.01f, 50.0f);

    // Camera looks from +Z toward origin, centered on target
    m_view = glm::lookAt(
        glm::vec3(m_targetX, m_targetY, 10.0f),
        glm::vec3(m_targetX, m_targetY, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::viewProj() const {
    return m_proj * m_view;
}

void Camera::frustumBounds(float& left, float& right, float& bottom, float& top) const {
    float halfH = 6.0f / (m_zoom / 100.0f);
    float halfW = halfH * m_aspect;
    left   = m_targetX - halfW;
    right  = m_targetX + halfW;
    bottom = m_targetY - halfH;
    top    = m_targetY + halfH;
}
