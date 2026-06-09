#include "PlanetTrail.h"
#include "render/Shader.h"
#include "camera/Camera.h"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>
#include <cstring>

PlanetTrail::PlanetTrail(const glm::vec3& color, float planetRadius)
    : m_planetRadius(planetRadius), m_color(color) {
}

PlanetTrail::~PlanetTrail() {
    if (m_ebo) glDeleteBuffers(1, &m_ebo);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
}

PlanetTrail::PlanetTrail(PlanetTrail&& o) noexcept
    : m_ring(std::move(o.m_ring)), m_head(o.m_head), m_count(o.m_count),
      m_lastTime(o.m_lastTime), m_center(o.m_center),
      m_trailDuration(o.m_trailDuration), m_planetRadius(o.m_planetRadius),
      m_color(o.m_color), m_verts(o.m_verts), m_indices(o.m_indices),
      m_vao(o.m_vao), m_vbo(o.m_vbo), m_ebo(o.m_ebo),
      m_vertexCount(o.m_vertexCount), m_indexCount(o.m_indexCount), m_dirty(o.m_dirty) {
    o.m_vao = o.m_vbo = o.m_ebo = 0;
    o.m_vertexCount = o.m_indexCount = 0;
}

PlanetTrail& PlanetTrail::operator=(PlanetTrail&& o) noexcept {
    if (this != &o) {
        if (m_ebo) glDeleteBuffers(1, &m_ebo);
        if (m_vbo) glDeleteBuffers(1, &m_vbo);
        if (m_vao) glDeleteVertexArrays(1, &m_vao);
        m_ring = std::move(o.m_ring); m_head = o.m_head; m_count = o.m_count;
        m_lastTime = o.m_lastTime; m_center = o.m_center;
        m_trailDuration = o.m_trailDuration; m_planetRadius = o.m_planetRadius;
        m_color = o.m_color; m_verts = o.m_verts; m_indices = o.m_indices;
        m_vao = o.m_vao; m_vbo = o.m_vbo; m_ebo = o.m_ebo;
        m_vertexCount = o.m_vertexCount; m_indexCount = o.m_indexCount;
        m_dirty = o.m_dirty;
        o.m_vao = o.m_vbo = o.m_ebo = 0;
        o.m_vertexCount = o.m_indexCount = 0;
    }
    return *this;
}

glm::vec2 PlanetTrail::catmullRom(const glm::vec2& p0, const glm::vec2& p1,
                                   const glm::vec2& p2, const glm::vec2& p3, float t) {
    float t2 = t * t, t3 = t2 * t;
    float c0 = -0.5f * t3 + t2 - 0.5f * t;
    float c1 =  1.5f * t3 - 2.5f * t2 + 1.0f;
    float c2 = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
    float c3 =  0.5f * t3 - 0.5f * t2;
    return c0 * p0 + c1 * p1 + c2 * p2 + c3 * p3;
}

glm::vec2 PlanetTrail::catmullRomTangent(const glm::vec2& p0, const glm::vec2& p1,
                                          const glm::vec2& p2, const glm::vec2& p3, float t) {
    float t2 = t * t;
    float c0 = -1.5f * t2 + 2.0f * t - 0.5f;
    float c1 =  4.5f * t2 - 5.0f * t;
    float c2 = -4.5f * t2 + 4.0f * t + 0.5f;
    float c3 =  1.5f * t2 - 1.0f * t;
    return c0 * p0 + c1 * p1 + c2 * p2 + c3 * p3;
}

void PlanetTrail::update(const glm::vec2& pos, float currentTime) {
    // Ring buffer insert (O(1), no shift)
    m_ring[m_head] = {pos, currentTime};
    m_head = (m_head + 1) % MAX_POINTS;
    if (m_count < MAX_POINTS) m_count++;
    m_lastTime = currentTime;

    // Expire old points by advancing count (no erase needed — they're overwritten naturally)
    // Check oldest point time
    while (m_count > 0) {
        int oldestIdx = (m_head - m_count + MAX_POINTS) % MAX_POINTS;
        if (currentTime - m_ring[oldestIdx].time > m_trailDuration)
            m_count--;
        else
            break;
    }

    m_dirty = true;
}

void PlanetTrail::clear() {
    m_head = 0;
    m_count = 0;
    m_lastTime = 0.0f;
    m_dirty = true;
}

void PlanetTrail::setPoints(const float* xy, int count) {
    if (count < 2) { m_head = 0; m_count = 0; m_dirty = true; return; }

    // Load into ring buffer
    count = std::min(count, MAX_POINTS);
    for (int i = 0; i < count; i++) {
        m_ring[i].pos = {xy[i * 2], xy[i * 2 + 1]};
        m_ring[i].time = 0.0f;
    }
    m_head = count % MAX_POINTS;
    m_count = count;

    ensureGPUResources();
    rebuildGeometry();
}

void PlanetTrail::ensureGPUResources() const {
    if (m_vao != 0) return;

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, MAX_VERTS * 3 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    glGenBuffers(1, &m_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, MAX_INDICES * sizeof(unsigned), nullptr, GL_DYNAMIC_DRAW);

    glBindVertexArray(0);
}

void PlanetTrail::rebuildGeometry() {
    ensureGPUResources();

    if (m_count < 2) { m_vertexCount = 0; m_indexCount = 0; return; }

    // Center point: keep VBO values small for float precision
    m_center = (ringAt(0).pos + ringAt(m_count - 1).pos) * 0.5f;

    int totalSegments = (m_count - 1) * SEGS_PER_POINT;
    int vertCount = (totalSegments + 1) * 2;
    int idxCount = totalSegments * 6;

    float maxWidth = m_planetRadius * 2.0f;
    int vi = 0;

    for (int seg = 0; seg <= totalSegments; seg++) {
        float globalT = (float)seg / (float)totalSegments;
        float rawIdx = globalT * (m_count - 1);
        int i = (int)rawIdx;
        float localT = rawIdx - (float)i;

        glm::vec2 p0 = ringAt(std::max(0, i - 1)).pos;
        glm::vec2 p1 = ringAt(i).pos;
        glm::vec2 p2 = ringAt(std::min(m_count - 1, i + 1)).pos;
        glm::vec2 p3 = ringAt(std::min(m_count - 1, i + 2)).pos;

        glm::vec2 pt = catmullRom(p0, p1, p2, p3, localT);
        glm::vec2 tangent = catmullRomTangent(p0, p1, p2, p3, localT);

        float len = std::sqrt(tangent.x * tangent.x + tangent.y * tangent.y);
        glm::vec2 normal = (len > 0.001f) ? glm::vec2(-tangent.y / len, tangent.x / len) : glm::vec2(0.0f, 1.0f);

        float width = maxWidth * globalT;

        m_verts[vi * 3 + 0] = pt.x - m_center.x - normal.x * width * 0.5f;
        m_verts[vi * 3 + 1] = pt.y - m_center.y - normal.y * width * 0.5f;
        m_verts[vi * 3 + 2] = 0.0f;
        vi++;

        m_verts[vi * 3 + 0] = pt.x - m_center.x + normal.x * width * 0.5f;
        m_verts[vi * 3 + 1] = pt.y - m_center.y + normal.y * width * 0.5f;
        m_verts[vi * 3 + 2] = 0.0f;
        vi++;
    }

    int ii = 0;
    for (int seg = 0; seg < totalSegments; seg++) {
        unsigned base = seg * 2;
        m_indices[ii++] = base;
        m_indices[ii++] = base + 1;
        m_indices[ii++] = base + 2;
        m_indices[ii++] = base + 1;
        m_indices[ii++] = base + 3;
        m_indices[ii++] = base + 2;
    }

    m_vertexCount = vertCount;
    m_indexCount = idxCount;

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertCount * 3 * sizeof(float), m_verts.data());
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, idxCount * sizeof(unsigned), m_indices.data());
    glBindVertexArray(0);

    m_dirty = false;
}

void PlanetTrail::draw(Shader& shader, const Camera& camera, double camX, double camY) {
    if (m_count < 2) return;

    if (m_dirty) rebuildGeometry();
    if (m_indexCount == 0) return;

    // VBO is relative to m_center, translate by (center - camTarget) → camera-relative
    auto model = glm::translate(glm::mat4(1.0f),
        glm::vec3(m_center.x - (float)camX, m_center.y - (float)camY, 0.0f));
    auto mvp = camera.viewProj() * model;

    shader.use();
    shader.setMat4("uMVP", glm::value_ptr(mvp));
    shader.setVec4("uColor", m_color.x, m_color.y, m_color.z, 0.6f);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}
