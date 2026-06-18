#include "Camera.h"

void Camera::setZoom(float zoom) {
    m_zoom = zoom;
    update();
}

void Camera::setAspect(float width, float height) {
    m_aspect = width / height;
    update();
}

void Camera::setTarget(double x, double y) {
    m_targetX = x;
    m_targetY = y;
    update();
}

void Camera::update() {
    float halfH = 6.0f / (m_zoom / 100.0f);
    float halfW = halfH * m_aspect;

    m_proj = glm::ortho(-halfW, halfW, -halfH, halfH, 0.1f, 200.0f);

    // View at origin — instance offsets handle camera-relative translation
    m_view = glm::lookAt(
        glm::vec3(0.0f, 0.0f, 10.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::viewProj() const {
    return m_proj * m_view;
}

void Camera::frustumBounds(float& left, float& right, float& bottom, float& top) const {
    float halfH = 6.0f / (m_zoom / 100.0f);
    float halfW = halfH * m_aspect;
    left   = (float)(m_targetX - (double)halfW);
    right  = (float)(m_targetX + (double)halfW);
    bottom = (float)(m_targetY - (double)halfH);
    top    = (float)(m_targetY + (double)halfH);
}
