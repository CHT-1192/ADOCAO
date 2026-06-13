#include "Shader.h"
#include <cstdio>
#include <vector>

Shader::~Shader() { destroy(); }

Shader::Shader(Shader&& other) noexcept : m_program(other.m_program) {
    other.m_program = 0;
}

Shader& Shader::operator=(Shader&& other) noexcept {
    if (this != &other) { destroy(); m_program = other.m_program; other.m_program = 0; }
    return *this;
}

void Shader::destroy() {
    if (m_program) { glDeleteProgram(m_program); m_program = 0; }
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
        const char* name = type == GL_VERTEX_SHADER ? "Vertex"
                         : type == GL_FRAGMENT_SHADER ? "Fragment"
                         : "Compute";
        fprintf(stderr, "[Shader] %s compile error:\n%s\n", name, log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool Shader::compileCompute(const char* compSrc) {
    destroy();
    GLuint cs = compileShader(GL_COMPUTE_SHADER, compSrc);
    if (!cs) return false;

    m_program = glCreateProgram();
    glAttachShader(m_program, cs);
    glLinkProgram(m_program);

    GLint success;
    glGetProgramiv(m_program, GL_LINK_STATUS, &success);
    if (!success) {
        char log[1024];
        glGetProgramInfoLog(m_program, sizeof(log), nullptr, log);
        fprintf(stderr, "[Shader] Compute link error:\n%s\n", log);
        destroy();
    }

    glDeleteShader(cs);
    return m_program != 0;
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

void Shader::dispatch(GLuint x, GLuint y, GLuint z) const {
    glUseProgram(m_program);
    glad_DispatchCompute(x, y, z);
}

void Shader::setMat4(const char* name, const float* value) const {
    glUniformMatrix4fv(glGetUniformLocation(m_program, name), 1, GL_FALSE, value);
}

void Shader::setVec4(const char* name, float x, float y, float z, float w) const {
    glUniform4f(glGetUniformLocation(m_program, name), x, y, z, w);
}

void Shader::setFloat(const char* name, float v) const {
    glUniform1f(glGetUniformLocation(m_program, name), v);
}
