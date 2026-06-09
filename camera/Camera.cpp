#include "Camera.h"

Camera::Camera() {
    // View matrix never changes — compute once
    m_view = glm::lookAt(
        glm::vec3(0.0f, 0.0f, 10.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f));
    updateProj();
}

void Camera::setZoom(float zoom) {
    m_zoom = zoom;
    updateProj();
}

void Camera::setAspect(float width, float height) {
    m_aspect = width / height;
    updateProj();
}

void Camera::setTarget(double x, double y) {
    // Just store — no matrix recomputation needed
    m_targetX = x;
    m_targetY = y;
}

void Camera::updateProj() {
    m_halfH = 6.0f / (m_zoom / 100.0f);
    m_halfW = m_halfH * m_aspect;
    m_proj = glm::ortho(-m_halfW, m_halfW, -m_halfH, m_halfH, 0.1f, 50000.0f);
}

void Camera::frustumBounds(float& left, float& right, float& bottom, float& top) const {
    // Use cached halfH/halfW — no recomputation
    left   = (float)(m_targetX - (double)m_halfW);
    right  = (float)(m_targetX + (double)m_halfW);
    bottom = (float)(m_targetY - (double)m_halfH);
    top    = (float)(m_targetY + (double)m_halfH);
}
