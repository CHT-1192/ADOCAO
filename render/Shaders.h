#pragma once

namespace Shaders {

// Instanced tile rendering. aInstOffset is world position (uploaded once at build).
// Camera-relative subtraction done in shader to eliminate per-frame CPU uploads.
constexpr const char* kTileVertSrc = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
layout(location = 2) in vec3 aInstOffset;  // world-space (static, uploaded once)
uniform mat4 uVP;
uniform vec2 uCam;                         // camera target in world coords
out vec3 vColor;
void main() {
    gl_Position = uVP * vec4(aPos + aInstOffset - vec3(uCam, 0.0), 1.0);
    vColor = aColor;
}
)";

constexpr const char* kTileFragSrc = R"(
#version 330 core
in vec3 vColor;
out vec4 fragColor;
void main() {
    fragColor = vec4(vColor, 1.0);
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

} // namespace Shaders
