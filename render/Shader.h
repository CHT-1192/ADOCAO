#pragma once

#include "glad/gl_core.h"
#include <string>
#include <unordered_map>

class Shader {
public:
    Shader() = default;
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    bool compile(const char* vertSrc, const char* fragSrc);
    void use() const;
    void destroy();

    GLuint id() const { return m_program; }

    // Uniform setters (location cached on first lookup)
    void setMat4(const char* name, const float* value) const;
    void setVec4(const char* name, float x, float y, float z, float w) const;
    void setFloat(const char* name, float v) const;
    void setVec2(const char* name, float x, float y) const;

private:
    GLuint m_program = 0;
    mutable std::unordered_map<std::string, GLint> m_uniformCache;

    GLint getUniformLoc(const char* name) const;

    static GLuint compileShader(GLenum type, const char* src);
};
