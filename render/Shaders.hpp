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

// Highlight shader: same vertex layout as tile, fragment inverts the original track color
constexpr const char* kHighlightVertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in float aType;
layout(location = 2) in vec3 aInstOffset;
layout(location = 3) in vec3 iColor;
layout(location = 4) in vec3 iBgColor;
layout(location = 5) in float iOpacity;
uniform mat4 uVP;
out vec3 vColor;
void main() {
    vColor = mix(iBgColor, iColor, aType);
    gl_Position = uVP * vec4(aPos + aInstOffset, 1.0);
}
)";

constexpr const char* kHighlightFragSrc = R"(
#version 330 core
in vec3 vColor;
out vec4 fragColor;
void main() {
    fragColor = vec4(1.0 - vColor, 1.0);  // inverted track color
}
)";


} // namespace Shaders
