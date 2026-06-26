#include "PlanetTrail.hpp"
#include "render/Shader.hpp"
#include "camera/Camera.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>

PlanetTrail::PlanetTrail(const glm::vec3& color, float planetRadius)
    : m_planetRadius(planetRadius), m_color(color) {
    m_points.resize(m_maxPoints);
}

PlanetTrail::~PlanetTrail() {
    if (m_ebo) glDeleteBuffers(1, &m_ebo);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
}

PlanetTrail::PlanetTrail(PlanetTrail&& o) noexcept
    : m_points(std::move(o.m_points)), m_head(o.m_head), m_count(o.m_count),
      m_verts(std::move(o.m_verts)), m_indices(std::move(o.m_indices)),
      m_center(o.m_center), m_maxPoints(o.m_maxPoints),
      m_trailDuration(o.m_trailDuration), m_planetRadius(o.m_planetRadius),
      m_color(o.m_color), m_vao(o.m_vao), m_vbo(o.m_vbo), m_ebo(o.m_ebo),
      m_vertexCount(o.m_vertexCount), m_indexCount(o.m_indexCount), m_dirty(o.m_dirty) {
    o.m_vao = o.m_vbo = o.m_ebo = 0;
    o.m_vertexCount = o.m_indexCount = 0;
}

PlanetTrail& PlanetTrail::operator=(PlanetTrail&& o) noexcept {
    if (this != &o) {
        if (m_ebo) glDeleteBuffers(1, &m_ebo);
        if (m_vbo) glDeleteBuffers(1, &m_vbo);
        if (m_vao) glDeleteVertexArrays(1, &m_vao);
        m_points = std::move(o.m_points); m_head = o.m_head; m_count = o.m_count;
        m_verts = std::move(o.m_verts); m_indices = std::move(o.m_indices);
        m_center = o.m_center; m_maxPoints = o.m_maxPoints;
        m_trailDuration = o.m_trailDuration; m_planetRadius = o.m_planetRadius;
        m_color = o.m_color;
        m_vao = o.m_vao; m_vbo = o.m_vbo; m_ebo = o.m_ebo;
        m_vertexCount = o.m_vertexCount; m_indexCount = o.m_indexCount;
        m_dirty = o.m_dirty;
        o.m_vao = o.m_vbo = o.m_ebo = 0;
        o.m_vertexCount = o.m_indexCount = 0;
    }
    return *this;
}

void PlanetTrail::ringPushBack(const Point& pt) {
    if (m_count < m_maxPoints) {
        m_points[(m_head + m_count) % m_maxPoints] = pt;
        m_count++;
    } else {
        m_points[m_head] = pt;
        m_head = (m_head + 1) % m_maxPoints;
    }
}

void PlanetTrail::ringPopFront() {
    if (m_count > 0) {
        m_head = (m_head + 1) % m_maxPoints;
        m_count--;
    }
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
    ringPushBack({pos, currentTime});
    while (m_count > 0 && currentTime - ringAt(0).time > m_trailDuration)
        ringPopFront();
    m_dirty = true;
}

void PlanetTrail::clear() {
    m_head = 0;
    m_count = 0;
    m_dirty = true;
}

void PlanetTrail::setPoints(const float* xy, int count) {
    if (count < 2) { clear(); return; }
    m_head = 0;
    m_count = std::min(count, m_maxPoints);
    for (int i = 0; i < m_count; i++) {
        m_points[i].pos = {xy[i*2], xy[i*2+1]};
        m_points[i].time = 0.0f;
    }
    ensureGPUResources();
    int n = m_count;
    m_center = (ringAt(0).pos + ringAt(n-1).pos) * 0.5f;
    const int segsPerPoint = 4;
    int totalSegments = (n-1) * segsPerPoint;
    int vertCount = (totalSegments+1)*2, idxCount = totalSegments*6;
    m_verts.resize(vertCount*3);
    m_indices.resize(idxCount);
    float maxWidth = m_planetRadius*2.0f;
    int vi=0;
    for (int seg=0; seg<=totalSegments; seg++) {
        float globalT = (float)seg/(float)totalSegments;
        float rawIdx = globalT*(n-1);
        int i=(int)rawIdx; float localT=rawIdx-(float)i;
        glm::vec2 p0=ringAt(std::max(0,i-1)).pos, p1=ringAt(i).pos;
        glm::vec2 p2=ringAt(std::min(n-1,i+1)).pos, p3=ringAt(std::min(n-1,i+2)).pos;
        glm::vec2 pt=catmullRom(p0,p1,p2,p3,localT);
        glm::vec2 tangent=catmullRomTangent(p0,p1,p2,p3,localT);
        float len=std::sqrt(tangent.x*tangent.x+tangent.y*tangent.y);
        glm::vec2 normal=(len>0.001f)?glm::vec2(-tangent.y/len,tangent.x/len):glm::vec2(0,1);
        float width=maxWidth*globalT;
        m_verts[vi*3]=pt.x-m_center.x-normal.x*width*0.5f; m_verts[vi*3+1]=pt.y-m_center.y-normal.y*width*0.5f; m_verts[vi*3+2]=0; vi++;
        m_verts[vi*3]=pt.x-m_center.x+normal.x*width*0.5f; m_verts[vi*3+1]=pt.y-m_center.y+normal.y*width*0.5f; m_verts[vi*3+2]=0; vi++;
    }
    int ii=0;
    for (int seg=0; seg<totalSegments; seg++) {
        unsigned base=seg*2;
        m_indices[ii++]=base; m_indices[ii++]=base+1; m_indices[ii++]=base+2;
        m_indices[ii++]=base+1; m_indices[ii++]=base+3; m_indices[ii++]=base+2;
    }
    m_vertexCount=vertCount; m_indexCount=idxCount; m_dirty=false;
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER,m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER,0,m_verts.size()*sizeof(float),m_verts.data());
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,m_ebo);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER,0,m_indices.size()*sizeof(unsigned),m_indices.data());
    glBindVertexArray(0);
}

void PlanetTrail::ensureGPUResources() const {
    if (m_vao != 0) return;
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);
    constexpr int segsPerPoint = 4;
    int maxSegments = (m_maxPoints - 1) * segsPerPoint;
    int maxVerts = (maxSegments + 1) * 2;
    int maxIndices = maxSegments * 6;
    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, maxVerts * 3 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glGenBuffers(1, &m_ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, maxIndices * sizeof(unsigned), nullptr, GL_DYNAMIC_DRAW);
    glBindVertexArray(0);
}

void PlanetTrail::rebuildGeometry() {
    ensureGPUResources();
    int n = m_count;
    if (n < 2) { m_vertexCount = 0; m_indexCount = 0; return; }
    m_center = (ringAt(0).pos + ringAt(n-1).pos) * 0.5f;
    const int segsPerPoint = 4;
    int totalSegments = (n-1)*segsPerPoint, vertCount = (totalSegments+1)*2, idxCount = totalSegments*6;
    m_verts.resize(vertCount*3);
    m_indices.resize(idxCount);
    float maxWidth = m_planetRadius*2.0f;
    int vi=0;
    for (int seg=0; seg<=totalSegments; seg++) {
        float globalT=(float)seg/(float)totalSegments, rawIdx=globalT*(n-1);
        int i=(int)rawIdx; float localT=rawIdx-(float)i;
        glm::vec2 p0=ringAt(std::max(0,i-1)).pos, p1=ringAt(i).pos;
        glm::vec2 p2=ringAt(std::min(n-1,i+1)).pos, p3=ringAt(std::min(n-1,i+2)).pos;
        glm::vec2 pt=catmullRom(p0,p1,p2,p3,localT);
        glm::vec2 tangent=catmullRomTangent(p0,p1,p2,p3,localT);
        float len=std::sqrt(tangent.x*tangent.x+tangent.y*tangent.y);
        glm::vec2 normal=(len>0.001f)?glm::vec2(-tangent.y/len,tangent.x/len):glm::vec2(0,1);
        float width=maxWidth*globalT;
        m_verts[vi*3]=pt.x-m_center.x-normal.x*width*0.5f; m_verts[vi*3+1]=pt.y-m_center.y-normal.y*width*0.5f; m_verts[vi*3+2]=0; vi++;
        m_verts[vi*3]=pt.x-m_center.x+normal.x*width*0.5f; m_verts[vi*3+1]=pt.y-m_center.y+normal.y*width*0.5f; m_verts[vi*3+2]=0; vi++;
    }
    int ii=0;
    for (int seg=0; seg<totalSegments; seg++) {
        unsigned base=seg*2;
        m_indices[ii++]=base; m_indices[ii++]=base+1; m_indices[ii++]=base+2;
        m_indices[ii++]=base+1; m_indices[ii++]=base+3; m_indices[ii++]=base+2;
    }
    m_vertexCount=vertCount; m_indexCount=idxCount;
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER,m_vbo);
    glBufferSubData(GL_ARRAY_BUFFER,0,m_verts.size()*sizeof(float),m_verts.data());
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,m_ebo);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER,0,m_indices.size()*sizeof(unsigned),m_indices.data());
    glBindVertexArray(0);
    m_dirty=false;
}

void PlanetTrail::draw(Shader& shader, const Camera& camera, double camX, double camY) {
    if (m_count < 2) return;
    if (m_dirty) rebuildGeometry();
    if (m_indexCount == 0) return;
    auto model = glm::translate(glm::mat4(1.0f),
        glm::vec3(m_center.x-(float)camX, m_center.y-(float)camY, 0));
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
