// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)

#include "core/shader.h"
#include "io/shader_io.h"

#include <glm/gtc/type_ptr.hpp>

#include <cstdio>
#include <vector>

namespace tubelight {

namespace {

GLuint compile_stage(GLenum type, std::string_view src, std::string& error_out) {
    GLuint shader = glCreateShader(type);
    const char* src_data = src.data();
    const auto length = static_cast<GLint>(src.size());
    glShaderSource(shader, 1, &src_data, &length);
    glCompileShader(shader);

    GLint status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        GLint log_len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
        std::vector<char> log(static_cast<size_t>(log_len) + 1, 0);
        glGetShaderInfoLog(shader, log_len, nullptr, log.data());
        error_out = std::string("compile error: ") + log.data();
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint link_program(GLuint vs, GLuint fs, std::string& error_out) {
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);

    GLint status = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (status == GL_FALSE) {
        GLint log_len = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);
        std::vector<char> log(static_cast<size_t>(log_len) + 1, 0);
        glGetProgramInfoLog(program, log_len, nullptr, log.data());
        error_out = std::string("link error: ") + log.data();
        glDeleteProgram(program);
        return 0;
    }

    // Shaders can be detached + deleted after a successful link.
    glDetachShader(program, vs);
    glDetachShader(program, fs);
    return program;
}

} // namespace

const char* default_fullscreen_vertex_source() {
    // Single-triangle technique: 3 vertices, no VBO needed beyond gl_VertexID.
    // Produces a triangle that covers [-1, 1] x [-1, 1] with uv in [0, 1].
    return R"(#version 450 core
out vec2 v_uv;
void main() {
    // Generate (0,0), (2,0), (0,2) from gl_VertexID = 0,1,2
    v_uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(v_uv * 2.0 - 1.0, 0.0, 1.0);
}
)";
}

ShaderProgram::~ShaderProgram() {
    destroy();
}

ShaderProgram::ShaderProgram(ShaderProgram&& other) noexcept
    : program_(other.program_),
      error_(std::move(other.error_)),
      uniform_cache_(std::move(other.uniform_cache_)) {
    other.program_ = 0;
}

ShaderProgram& ShaderProgram::operator=(ShaderProgram&& other) noexcept {
    if (this != &other) {
        destroy();
        program_ = other.program_;
        error_ = std::move(other.error_);
        uniform_cache_ = std::move(other.uniform_cache_);
        other.program_ = 0;
    }
    return *this;
}

void ShaderProgram::destroy() {
    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }
    uniform_cache_.clear();
}

bool ShaderProgram::build_from_source(std::string_view vertex_src, std::string_view fragment_src) {
    destroy();
    error_.clear();

    const std::string_view vs_src = vertex_src.empty()
        ? std::string_view(default_fullscreen_vertex_source())
        : vertex_src;

    GLuint vs = compile_stage(GL_VERTEX_SHADER, vs_src, error_);
    if (vs == 0) {
        return false;
    }
    GLuint fs = compile_stage(GL_FRAGMENT_SHADER, fragment_src, error_);
    if (fs == 0) {
        glDeleteShader(vs);
        return false;
    }

    program_ = link_program(vs, fs, error_);

    glDeleteShader(vs);
    glDeleteShader(fs);

    return program_ != 0;
}

bool ShaderProgram::build_from_files(std::string_view vertex_path, std::string_view fragment_path) {
    std::string vs_src;
    if (!vertex_path.empty()) {
        if (!read_text_file(std::string(vertex_path), vs_src, error_)) {
            return false;
        }
    }
    std::string fs_src;
    if (!read_text_file(std::string(fragment_path), fs_src, error_)) {
        return false;
    }
    return build_from_source(vs_src, fs_src);
}

void ShaderProgram::use() const {
    if (program_ != 0) {
        glUseProgram(program_);
    }
}

GLint ShaderProgram::uniform_location(std::string_view name) {
    if (program_ == 0) {
        return -1;
    }
    std::string key(name);
    auto it = uniform_cache_.find(key);
    if (it != uniform_cache_.end()) {
        return it->second;
    }
    GLint loc = glGetUniformLocation(program_, key.c_str());
    uniform_cache_.emplace(std::move(key), loc);
    return loc;
}

bool ShaderProgram::set_int(std::string_view name, int value) {
    GLint loc = uniform_location(name);
    if (loc < 0) return false;
    glUniform1i(loc, value);
    return true;
}
bool ShaderProgram::set_float(std::string_view name, float value) {
    GLint loc = uniform_location(name);
    if (loc < 0) return false;
    glUniform1f(loc, value);
    return true;
}
bool ShaderProgram::set_vec2(std::string_view name, glm::vec2 v) {
    GLint loc = uniform_location(name);
    if (loc < 0) return false;
    glUniform2f(loc, v.x, v.y);
    return true;
}
bool ShaderProgram::set_vec3(std::string_view name, glm::vec3 v) {
    GLint loc = uniform_location(name);
    if (loc < 0) return false;
    glUniform3f(loc, v.x, v.y, v.z);
    return true;
}
bool ShaderProgram::set_vec4(std::string_view name, glm::vec4 v) {
    GLint loc = uniform_location(name);
    if (loc < 0) return false;
    glUniform4f(loc, v.x, v.y, v.z, v.w);
    return true;
}
bool ShaderProgram::set_mat4(std::string_view name, const glm::mat4& m) {
    GLint loc = uniform_location(name);
    if (loc < 0) return false;
    glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(m));
    return true;
}

} // namespace tubelight
