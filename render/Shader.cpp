#include "Shader.h"
#include <cstdio>
#include <vector>

Shader::~Shader() { destroy(); }

Shader::Shader(Shader&& other) noexcept : m_program(other.m_program), m_uniformCache(std::move(other.m_uniformCache)) {
    other.m_program = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) { destroy(); m_program = other.m_program; m_uniformCache = std::move(other.m_uniformCache); other.m_program = 0; }
    return *this;
}

void Shader::destroy() {
    if (m_program) { glDeleteProgram(m_program); m_program = 0; }
    m_uniformCache.clear();
}

GLint Shader::getUniformLoc(const char* name) const {
    auto it = m_uniformCache.find(name);
    if (it != m_uniformCache.end()) return it->second;
    GLint loc = glGetUniformLocation(m_program, name);
    m_uniformCache[name] = loc;
    return loc;
}

GLuint Shader::compileShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        fprintf(stderr, "[Shader] %s compile error:\n%s\n",
                type == GL_VERTEX_SHADER ? "Vertex" : "Fragment", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool Shader::compile(const char* vertSrc, const char* fragSrc) {
    destroy();
    GLuint vs = compileShader(GL_VERTEX_SHADER, vertSrc);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragSrc);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }

    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glLinkProgram(m_program);

    GLint success;
    glGetProgramiv(m_program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetProgramInfoLog(m_program, sizeof(log), nullptr, log);
        fprintf(stderr, "[Shader] Link error:\n%s\n", log);
        destroy();
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return m_program != 0;
}

void Shader::use() const {
    glUseProgram(m_program);
}

void Shader::setMat4(const char* name, const float* value) const {
    glUniformMatrix4fv(getUniformLoc(name), 1, GL_FALSE, value);
}

void Shader::setVec4(const char* name, float x, float y, float z, float w) const {
    glUniform4f(getUniformLoc(name), x, y, z, w);
}

void Shader::setFloat(const char* name, float v) const {
    glUniform1f(getUniformLoc(name), v);
}

void Shader::setVec2(const char* name, float x, float y) const {
    glUniform2f(getUniformLoc(name), x, y);
}
