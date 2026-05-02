#include "TileMesh.h"
#include "../glad/gl_core.h"
#include "../util/Logger.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace {

constexpr float TILE_WIDTH = 0.275f;
constexpr float TILE_LENGTH = 0.5f;
constexpr float OUTLINE = 0.025f;

float fmod(float x, float y) {
    return x >= 0 ? std::fmod(x, y) : std::fmod(x, y) + y;
}

float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

// Create a circle (fan of triangles) and append to vertices/indices/colors
void createCircle(
    float cx, float cy, float cz, float radius,
    float r, float g, float b,
    std::vector<float>& verts,
    std::vector<unsigned int>& indices,
    std::vector<float>& colors,
    int resolution = 32)
{
    unsigned int centerIdx = static_cast<unsigned int>(verts.size() / 3);
    verts.insert(verts.end(), {cx, cy, cz});
    colors.insert(colors.end(), {r, g, b});

    for (int i = 0; i < resolution; i++) {
        float angle = (2.0f * 3.14159265f * i) / resolution;
        float x = std::cos(angle) * radius + cx;
        float y = std::sin(angle) * radius + cy;
        verts.insert(verts.end(), {x, y, cz});
        colors.insert(colors.end(), {r, g, b});
    }

    for (int i = 1; i < resolution; i++) {
        indices.insert(indices.end(), {centerIdx, centerIdx + (unsigned int)i, centerIdx + (unsigned int)(i + 1)});
    }
    indices.insert(indices.end(), {centerIdx, centerIdx + (unsigned int)resolution, centerIdx + 1});
}

// Create a midspin (180°) tile mesh in local coords
void createMidSpinMesh(
    float angle, float r, float g, float b,
    std::vector<float>& verts,
    std::vector<unsigned int>& indices,
    std::vector<float>& colors)
{
    float w = TILE_WIDTH + OUTLINE;
    float l = TILE_WIDTH + OUTLINE;

    float m1 = std::cos(angle * 3.14159265f / 180.0f);
    float m2 = std::sin(angle * 3.14159265f / 180.0f);
    float mx = -m1 * 0.04f, my = -m2 * 0.04f;

    // Outer (outline)
    unsigned int base = static_cast<unsigned int>(verts.size() / 3);
    verts.insert(verts.end(), {
        mx + l * m1 + w * m2, my + l * m2 - w * m1, 0,
        mx + l * m1 - w * m2, my + l * m2 + w * m1, 0,
        mx - w * m2,           my + w * m1,           0,
        mx + w * m2,           my - w * m1,           0,
        mx - w * m1,           my - w * m2,           0,
        mx + w * m2,           my - w * m1,           0,
        mx - w * m2,           my + w * m1,           0,
    });
    for (int i = 0; i < 7; i++) colors.insert(colors.end(), {0, 0, 0});  // black outline
    indices.insert(indices.end(), {base, base+1, base+2, base+2, base+3, base, base+4, base+5, base+6});

    // Inner (white)
    float innerW = TILE_WIDTH;
    float innerL = TILE_WIDTH;
    unsigned int base2 = static_cast<unsigned int>(verts.size() / 3);
    verts.insert(verts.end(), {
        mx + innerL * m1 + innerW * m2, my + innerL * m2 - innerW * m1, 0,
        mx + innerL * m1 - innerW * m2, my + innerL * m2 + innerW * m1, 0,
        mx - innerW * m2,               my + innerW * m1,               0,
        mx + innerW * m2,               my - innerW * m1,               0,
        mx - innerW * m1,               my - innerW * m2,               0,
        mx + innerW * m2,               my - innerW * m1,               0,
        mx - innerW * m2,               my + innerW * m1,               0,
    });
    for (int i = 0; i < 7; i++) colors.insert(colors.end(), {r, g, b});
    indices.insert(indices.end(), {base2, base2+1, base2+2, base2+2, base2+3, base2, base2+4, base2+5, base2+6});
}

// Main tile mesh creation — matches re_adojas createTileMesh exactly
void createTileMesh(
    float startAngle, float endAngle,
    float r, float g, float b,
    std::vector<float>& verts,
    std::vector<unsigned int>& indices,
    std::vector<float>& colors)
{
    float width  = TILE_WIDTH;
    float length = TILE_LENGTH;

    float m11 = std::cos(startAngle * 3.14159265f / 180.0f);
    float m12 = std::sin(startAngle * 3.14159265f / 180.0f);
    float m21 = std::cos(endAngle   * 3.14159265f / 180.0f);
    float m22 = std::sin(endAngle   * 3.14159265f / 180.0f);

    float a0, a1;
    if (fmod(startAngle - endAngle, 360.0f) >= fmod(endAngle - startAngle, 360.0f)) {
        a0 = fmod(startAngle, 360.0f) * 3.14159265f / 180.0f;
        a1 = a0 + fmod(endAngle - startAngle, 360.0f) * 3.14159265f / 180.0f;
    } else {
        a0 = fmod(endAngle, 360.0f) * 3.14159265f / 180.0f;
        a1 = a0 + fmod(startAngle - endAngle, 360.0f) * 3.14159265f / 180.0f;
    }

    float angle = a1 - a0;
    float mid   = a0 + angle / 2.0f;

    if (angle < 2.0943952f && angle > 0.0f) {
        // Small angle — curved with circle at corner
        float x;
        if (angle < 0.08726646f) {
            x = 1.0f;
        } else if (angle < 0.5235988f) {
            x = lerp(1.0f, 0.83f, std::pow((angle - 0.08726646f) / 0.43633235f, 0.5f));
        } else if (angle < 0.7853982f) {
            x = lerp(0.83f, 0.77f, std::pow((angle - 0.5235988f) / 0.2617994f, 1.0f));
        } else if (angle < 1.5707964f) {
            x = lerp(0.77f, 0.15f, std::pow((angle - 0.7853982f) / 0.7853982f, 0.7f));
        } else {
            x = lerp(0.15f, 0.0f, std::pow((angle - 1.5707964f) / 0.5235988f, 0.5f));
        }

        float distance, radius;
        if (x == 1.0f) {
            distance = 0.0f;
            radius = width;
        } else {
            radius = lerp(0.0f, width, x);
            distance = (width - radius) / std::sin(angle / 2.0f);
        }

        float circlex = -distance * std::cos(mid);
        float circley = -distance * std::sin(mid);

        // Outline
        float ow = width + OUTLINE, ol = length + OUTLINE, orad = radius + OUTLINE;
        float ocx = circlex, ocy = circley;

        createCircle(ocx, ocy, 0, orad, 0, 0, 0, verts, indices, colors);
        unsigned int cnt;

        cnt = static_cast<unsigned int>(verts.size() / 3);
        verts.insert(verts.end(), {
            -orad * std::sin(a1) + ocx, orad * std::cos(a1) + ocy, 0,
            ocx, ocy, 0,
            orad * std::sin(a0) + ocx, -orad * std::cos(a0) + ocy, 0,
            ow * std::sin(a0), -ow * std::cos(a0), 0,
            0, 0, 0,
            -ow * std::sin(a1), ow * std::cos(a1), 0,
        });
        for (int i = 0; i < 6; i++) colors.insert(colors.end(), {0, 0, 0});
        indices.insert(indices.end(), {cnt, cnt+1, cnt+5, cnt+4, cnt+1, cnt+5, cnt+2, cnt+3, cnt+4, cnt+1, cnt+3, cnt+4});

        cnt = static_cast<unsigned int>(verts.size() / 3);
        verts.insert(verts.end(), {
            ol * m11 + ow * m12, ol * m12 - ow * m11, 0,
            ol * m11 - ow * m12, ol * m12 + ow * m11, 0,
            -ow * m12, ow * m11, 0,
            ow * m12, -ow * m11, 0,
            ol * m21 + ow * m22, ol * m22 - ow * m21, 0,
            ol * m21 - ow * m22, ol * m22 + ow * m21, 0,
            -ow * m22, ow * m21, 0,
            ow * m22, -ow * m21, 0,
        });
        for (int i = 0; i < 8; i++) colors.insert(colors.end(), {0, 0, 0});
        indices.insert(indices.end(), {cnt, cnt+1, cnt+2, cnt+2, cnt+3, cnt, cnt+4, cnt+5, cnt+6, cnt+6, cnt+7, cnt+4});

        // Inner
        width -= OUTLINE * 2; length -= OUTLINE * 2; radius -= OUTLINE * 2;
        if (radius < 0) {
            radius = 0;
            circlex = (-width / std::sin(angle / 2.0f)) * std::cos(mid);
            circley = (-width / std::sin(angle / 2.0f)) * std::sin(mid);
        }
        createCircle(circlex, circley, 0, radius, r, g, b, verts, indices, colors);

        cnt = static_cast<unsigned int>(verts.size() / 3);
        verts.insert(verts.end(), {
            -radius * std::sin(a1) + circlex, radius * std::cos(a1) + circley, 0,
            circlex, circley, 0,
            radius * std::sin(a0) + circlex, -radius * std::cos(a0) + circley, 0,
            width * std::sin(a0), -width * std::cos(a0), 0,
            0, 0, 0,
            -width * std::sin(a1), width * std::cos(a1), 0,
        });
        for (int i = 0; i < 6; i++) colors.insert(colors.end(), {r, g, b});
        indices.insert(indices.end(), {cnt, cnt+1, cnt+5, cnt+4, cnt+1, cnt+5, cnt+2, cnt+3, cnt+4, cnt+1, cnt+3, cnt+4});

        cnt = static_cast<unsigned int>(verts.size() / 3);
        verts.insert(verts.end(), {
            length * m11 + width * m12, length * m12 - width * m11, 0,
            length * m11 - width * m12, length * m12 + width * m11, 0,
            -width * m12, width * m11, 0,
            width * m12, -width * m11, 0,
            length * m21 + width * m22, length * m22 - width * m21, 0,
            length * m21 - width * m22, length * m22 + width * m21, 0,
            -width * m22, width * m21, 0,
            width * m22, -width * m21, 0,
        });
        for (int i = 0; i < 8; i++) colors.insert(colors.end(), {r, g, b});
        indices.insert(indices.end(), {cnt, cnt+1, cnt+2, cnt+2, cnt+3, cnt, cnt+4, cnt+5, cnt+6, cnt+6, cnt+7, cnt+4});

    } else if (angle > 0.0f) {
        // Normal angle case
        width += OUTLINE; length += OUTLINE;
        float circlex = (-width / std::sin(angle / 2.0f)) * std::cos(mid);
        float circley = (-width / std::sin(angle / 2.0f)) * std::sin(mid);

        unsigned int cnt = static_cast<unsigned int>(verts.size() / 3);
        verts.insert(verts.end(), {
            circlex, circley, 0,
            width * std::sin(a0), -width * std::cos(a0), 0,
            0, 0, 0,
            -width * std::sin(a1), width * std::cos(a1), 0,
        });
        for (int i = 0; i < 4; i++) colors.insert(colors.end(), {0, 0, 0});
        indices.insert(indices.end(), {cnt, cnt+1, cnt+2, cnt+2, cnt+3, cnt});

        cnt = static_cast<unsigned int>(verts.size() / 3);
        verts.insert(verts.end(), {
            length * m11 + width * m12, length * m12 - width * m11, 0,
            length * m11 - width * m12, length * m12 + width * m11, 0,
            -width * m12, width * m11, 0,
            width * m12, -width * m11, 0,
            length * m21 + width * m22, length * m22 - width * m21, 0,
            length * m21 - width * m22, length * m22 + width * m21, 0,
            -width * m22, width * m21, 0,
            width * m22, -width * m21, 0,
        });
        for (int i = 0; i < 8; i++) colors.insert(colors.end(), {0, 0, 0});
        indices.insert(indices.end(), {cnt, cnt+1, cnt+2, cnt+2, cnt+3, cnt, cnt+4, cnt+5, cnt+6, cnt+6, cnt+7, cnt+4});

        // Inner
        width -= OUTLINE * 2; length -= OUTLINE * 2;
        circlex = (-width / std::sin(angle / 2.0f)) * std::cos(mid);
        circley = (-width / std::sin(angle / 2.0f)) * std::sin(mid);

        cnt = static_cast<unsigned int>(verts.size() / 3);
        verts.insert(verts.end(), {
            circlex, circley, 0,
            width * std::sin(a0), -width * std::cos(a0), 0,
            0, 0, 0,
            -width * std::sin(a1), width * std::cos(a1), 0,
        });
        for (int i = 0; i < 4; i++) colors.insert(colors.end(), {r, g, b});
        indices.insert(indices.end(), {cnt, cnt+1, cnt+2, cnt+2, cnt+3, cnt});

        cnt = static_cast<unsigned int>(verts.size() / 3);
        verts.insert(verts.end(), {
            length * m11 + width * m12, length * m12 - width * m11, 0,
            length * m11 - width * m12, length * m12 + width * m11, 0,
            -width * m12, width * m11, 0,
            width * m12, -width * m11, 0,
            length * m21 + width * m22, length * m22 - width * m21, 0,
            length * m21 - width * m22, length * m22 + width * m21, 0,
            -width * m22, width * m21, 0,
            width * m22, -width * m21, 0,
        });
        for (int i = 0; i < 8; i++) colors.insert(colors.end(), {r, g, b});
        indices.insert(indices.end(), {cnt, cnt+1, cnt+2, cnt+2, cnt+3, cnt, cnt+4, cnt+5, cnt+6, cnt+6, cnt+7, cnt+4});

    } else {
        // angle == 0 — 180° case (straight line continuation)
        length = width;
        width += OUTLINE; length += OUTLINE;
        float m1 = m11, m2 = m12;
        float mx = -m1 * 0.04f, my = -m2 * 0.04f;

        createCircle(mx, my, 0, width, 0, 0, 0, verts, indices, colors);

        unsigned int cnt = static_cast<unsigned int>(verts.size() / 3);
        verts.insert(verts.end(), {
            mx + length * m1 + width * m2, my + length * m2 - width * m1, 0,
            mx + length * m1 - width * m2, my + length * m2 + width * m1, 0,
            mx - width * m2, my + width * m1, 0,
            mx + width * m2, my - width * m1, 0,
        });
        for (int i = 0; i < 4; i++) colors.insert(colors.end(), {0, 0, 0});
        indices.insert(indices.end(), {cnt, cnt+1, cnt+2, cnt+2, cnt+3, cnt});

        // Inner
        width -= OUTLINE * 2; length -= OUTLINE * 2;
        createCircle(mx, my, 0, width, r, g, b, verts, indices, colors);

        cnt = static_cast<unsigned int>(verts.size() / 3);
        verts.insert(verts.end(), {
            mx + length * m1 + width * m2, my + length * m2 - width * m1, 0,
            mx + length * m1 - width * m2, my + length * m2 + width * m1, 0,
            mx - width * m2, my + width * m1, 0,
            mx + width * m2, my - width * m1, 0,
        });
        for (int i = 0; i < 4; i++) colors.insert(colors.end(), {r, g, b});
        indices.insert(indices.end(), {cnt, cnt+1, cnt+2, cnt+2, cnt+3, cnt});
    }
}

} // namespace

// ---- TileMesh class ----

struct Vertex {
    float x, y, z;
    float r, g, b;
};

TileMesh::~TileMesh() { destroy(); }

TileMesh::TileMesh(TileMesh&& other) noexcept
    : m_vao(other.m_vao), m_vbo(other.m_vbo), m_indexCount(other.m_indexCount) {
    other.m_vao = 0; other.m_vbo = 0; other.m_indexCount = 0;
}

TileMesh& TileMesh::operator=(TileMesh&& other) noexcept {
    if (this != &other) { destroy(); m_vao = other.m_vao; m_vbo = other.m_vbo;
        m_indexCount = other.m_indexCount; other.m_vao = 0; other.m_vbo = 0; other.m_indexCount = 0; }
    return *this;
}

void TileMesh::destroy() {
    if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    m_indexCount = 0;
}

void TileMesh::build(const LevelData& level) {
    destroy();
    const auto& tiles = level.tiles;
    if (tiles.size() < 2) return;

    int n = static_cast<int>(tiles.size()) - 1; // skip extra tile

    // Build combined vertex/index/color arrays for all tiles
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;

    for (int i = 0; i < n; i++) {
        const auto& tile = tiles[i];

        // startAngle: direction from previous tile reversed (= enter this tile)
        // endAngle: direction from this tile (= leave this tile)
        float startAngle, endAngle;
        bool isMidspin = false;

        if (i == 0) {
            startAngle = -180.0f;  // enter from left
            endAngle = tile.direction;
        } else {
            const auto& prev = tiles[i - 1];
            startAngle = prev.direction - 180.0f;
            endAngle = tile.direction;
        }

        float diff = endAngle - startAngle;
        if (std::abs(diff) < 0.01f || std::abs(diff - 360.0f) < 0.01f) {
            isMidspin = true;
        }

        // Use white for inner (like re_adojas), track color via shader uniform later
        float cr = 1.0f, cg = 1.0f, cb = 1.0f;

        // Generate mesh for this tile in local coords
        std::vector<float> localVerts;
        std::vector<unsigned int> localIdx;
        std::vector<float> localColors;

        if (isMidspin) {
            createMidSpinMesh(endAngle, cr, cg, cb, localVerts, localIdx, localColors);
        } else {
            createTileMesh(startAngle, endAngle, cr, cg, cb, localVerts, localIdx, localColors);
        }

        // Translate to world position and append to global buffers
        float wx = tile.position[0];
        float wy = tile.position[1];
        float z  = 0.0f;  // uniform Z: avoid tile N+1 outline covering tile N inner

        unsigned int baseIdx = static_cast<unsigned int>(vertices.size());
        unsigned int localVertCount = static_cast<unsigned int>(localVerts.size() / 3);

        for (unsigned int vi = 0; vi < localVertCount; vi++) {
            Vertex v;
            v.x = localVerts[vi * 3]     + wx;
            v.y = localVerts[vi * 3 + 1] + wy;
            v.z = z;
            v.r = localColors[vi * 3];
            v.g = localColors[vi * 3 + 1];
            v.b = localColors[vi * 3 + 2];
            vertices.push_back(v);
        }

        for (unsigned int idx : localIdx) {
            indices.push_back(baseIdx + idx);
        }
    }

    // Upload to GPU
    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);

    glGenBuffers(1, &m_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, x));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, r));

    GLuint ebo;
    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    m_indexCount = static_cast<GLsizei>(indices.size());

    glBindVertexArray(0);
    glDeleteBuffers(1, &ebo);

    LOG_I("Built track mesh: %d tiles (%zu verts, %d indices)",
           n, vertices.size(), m_indexCount);
}

void TileMesh::draw() const {
    if (m_indexCount == 0) return;
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

unsigned int TileMesh::hexToUInt(const std::string& hex) {
    unsigned int v = 0;
    for (char c : hex) {
        v <<= 4;
        if (c >= '0' && c <= '9')      v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else break;
    }
    return v;
}
