#pragma once

#include "glad/gl_core.hpp"
#include <string>

class Shader {
public:
    Shader() = default;
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&& other) noexcept;
    Shader& operator=(Shader&& other) noexcept;

    bool compile(const char* vertSrc, const char* fragSrc);
    bool compileCompute(const char* compSrc);
    bool compileFile(const char* vertPath, const char* fragPath);
    bool compileComputeFile(const char* compPath);
    void use() const;
    void dispatch(GLuint x, GLuint y = 1, GLuint z = 1) const;
    void destroy();

    GLuint id() const { return m_program; }

    // Uniform setters
    void setMat4(const char* name, const float* value) const;
    void setVec4(const char* name, float x, float y, float z, float w) const;
    void setFloat(const char* name, float v) const;

private:
    GLuint m_program = 0;

    static GLuint compileShader(GLenum type, const char* src);
};
