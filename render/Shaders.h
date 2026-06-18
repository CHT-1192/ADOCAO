#pragma once

namespace Shaders {

// Instanced tile rendering. Vertex type (0=stroke, 1=fill) mixes per-instance colors.
// Camera-relative offsets computed on CPU side.
constexpr const char* kTileVertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in float aType;        // 0.0=stroke, 1.0=fill
layout(location = 2) in vec3 aInstOffset;   // per-instance camera-relative offset
layout(location = 3) in vec3 iColor;         // fill color (per-instance)
layout(location = 4) in vec3 iBgColor;      // stroke color (per-instance)
layout(location = 5) in float iOpacity;     // opacity (per-instance)
uniform mat4 uVP;
out vec3 vColor;
out float vOpacity;
void main() {
    vColor = mix(iBgColor, iColor, aType);
    vOpacity = iOpacity;
    gl_Position = uVP * vec4(aPos + aInstOffset, 1.0);
}
)";

constexpr const char* kTileFragSrc = R"(
#version 330 core
in vec3 vColor;
in float vOpacity;
out vec4 fragColor;
void main() {
    fragColor = vec4(vColor, vOpacity);
}
)";

// Planet rendering: per-vertex position, uniform color and MVP
constexpr const char* kPlanetVertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

constexpr const char* kPlanetFragSrc = R"(
#version 330 core
uniform vec4 uColor;
out vec4 fragColor;
void main() {
    fragColor = uColor;
}
)";

// Trail rendering: per-vertex position, uniform color with alpha
constexpr const char* kTrailVertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

constexpr const char* kTrailFragSrc = R"(
#version 330 core
uniform vec4 uColor;
out vec4 fragColor;
void main() {
    fragColor = uColor;
}
)";

// Highlight shader: same vertex layout as tile, fragment outputs white (for inversion blend)
constexpr const char* kHighlightVertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in float aType;
layout(location = 2) in vec3 aInstOffset;
uniform mat4 uVP;
void main() {
    gl_Position = uVP * vec4(aPos + aInstOffset, 1.0);
}
)";

constexpr const char* kHighlightFragSrc = R"(
#version 330 core
out vec4 fragColor;
void main() {
    fragColor = vec4(1.0);  // white → glBlendFunc(ONE_MINUS_DST_COLOR, ZERO) inverts
}
)";

// ---- Compute shaders (OpenGL 4.3+) ----

// GPU frustum culling: tests all tile AABBs against 6 frustum planes.
// Writes visibility flags + camera-relative offsets for visible tiles.
constexpr const char* kTileCullCompSrc = R"(
#version 430 core
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(std140) uniform FrustumBlock {
    vec4 frustum[6];     // 6 plane equations (nx, ny, nz, d)
    vec2 camWorld;        // camera target (x, y) in world space
} uFrustum;

layout(std430, binding = 0) readonly buffer TileBounds {
    vec4 bounds[];  // [minX, minY, maxX, maxY] per tile, 2 vec4 per tile
} uTileBounds;

layout(std430, binding = 1) readonly buffer TilePositions {
    vec4 worldPos[];  // [x, y, z, padding] per tile
} uTilePositions;

layout(std430, binding = 2) buffer VisibleFlags {
    uint flags[];
} uVisible;

layout(std430, binding = 3) buffer InstanceOffsets {
    float offsets[];
} uOffsets;

bool frustumCull(vec3 bmin, vec3 bmax) {
    for (int p = 0; p < 6; p++) {
        vec3 pv;
        pv.x = uFrustum.frustum[p].x > 0.0 ? bmax.x : bmin.x;
        pv.y = uFrustum.frustum[p].y > 0.0 ? bmax.y : bmin.y;
        pv.z = uFrustum.frustum[p].z > 0.0 ? bmax.z : bmin.z;
        if (dot(uFrustum.frustum[p].xyz, pv) + uFrustum.frustum[p].w < 0.0)
            return true;
    }
    return false;
}

void main() {
    uint tid = gl_GlobalInvocationID.x;
    uint count = uint(uTilePositions.worldPos.length());
    if (tid >= count) return;

    vec4 bmin = uTileBounds.bounds[tid * 2u];
    vec4 bmax = uTileBounds.bounds[tid * 2u + 1u];

    if (frustumCull(bmin.xyz, bmax.xyz)) {
        uVisible.flags[tid] = 0u;
        return;
    }

    uVisible.flags[tid] = 1u;

    vec4 wp = uTilePositions.worldPos[tid];
    uint offIdx = tid * 3u;
    uOffsets.offsets[offIdx]     = wp.x - uFrustum.camWorld.x;
    uOffsets.offsets[offIdx + 1u] = wp.y - uFrustum.camWorld.y;
    uOffsets.offsets[offIdx + 2u] = wp.z;
}
)";

// GPU camera-relative offset computation (no culling — simpler fallback).
constexpr const char* kTileOffsetCompSrc = R"(
#version 430 core
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(std140) uniform CameraBlock {
    vec2 camWorld;
} uCamera;

layout(std430, binding = 0) readonly buffer TileBuffer {
    vec4 worldPositions[];
} uTiles;

layout(std430, binding = 1) buffer OffsetBuffer {
    float offsets[];
} uOffsets;

void main() {
    uint tid = gl_GlobalInvocationID.x;
    uint count = uint(uTiles.worldPositions.length());
    if (tid >= count) return;

    vec4 wp = uTiles.worldPositions[tid];
    uint base = tid * 3u;
    uOffsets.offsets[base]     = wp.x - uCamera.camWorld.x;
    uOffsets.offsets[base + 1u] = wp.y - uCamera.camWorld.y;
    uOffsets.offsets[base + 2u] = wp.z;
}
)";

} // namespace Shaders
