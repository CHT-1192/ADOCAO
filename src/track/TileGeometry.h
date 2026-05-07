#pragma once

#include <vector>
#include <cmath>

// Scratch buffer for geometry generation
struct Scratch {
    std::vector<float> verts;      // local xyz
    std::vector<float> colors;     // r,g,b per vertex
    std::vector<unsigned> indices;

    void clear() { verts.clear(); colors.clear(); indices.clear(); }
};

extern Scratch g_sc;

// Constants matching re_adojas
constexpr float TILE_WIDTH = 0.275f;
constexpr float TILE_LENGTH = 0.5f;
constexpr float OUTLINE = 0.025f;

// Helpers
inline float fmodWrap(float x, float y) { return x >= 0 ? std::fmod(x, y) : std::fmod(x, y) + y; }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

void pushColor(std::vector<float>& c, float r, float g, float b, int n);

// Geometry generators (local-space, origin-centered)
void createCircle(float cx, float cy, float radius, float cr, float cg, float cb, Scratch& sc, int res = 32);
void createMidSpinMesh(float angle, Scratch& sc);
void createTileMesh(float startAngle, float endAngle, Scratch& sc);
