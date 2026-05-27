// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)

#pragma once

#include "gl_common.h"

#include <glm/glm.hpp>

#include <string>
#include <string_view>
#include <unordered_map>

namespace tubelight {

class ShaderProgram {
public:
    ShaderProgram() = default;
    ~ShaderProgram();

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;
    ShaderProgram(ShaderProgram&& other) noexcept;
    ShaderProgram& operator=(ShaderProgram&& other) noexcept;

    // Build from inline source strings. Returns false on failure; details in
    // get_error(). Vertex source can be empty to use the default fullscreen vert.
    bool build_from_source(std::string_view vertex_src, std::string_view fragment_src);

    // Build from on-disk files relative to TUBELIGHT_SHADER_DIR.
    // If vertex_path is empty, uses the embedded fullscreen vertex shader.
    bool build_from_files(std::string_view vertex_path, std::string_view fragment_path);

    void use() const;
    GLuint program_id() const { return program_; }

    // Uniform setters — return false silently if name is not active in the program.
    bool set_int(std::string_view name, int value);
    bool set_float(std::string_view name, float value);
    bool set_vec2(std::string_view name, glm::vec2 v);
    bool set_vec3(std::string_view name, glm::vec3 v);
    bool set_vec4(std::string_view name, glm::vec4 v);
    bool set_mat4(std::string_view name, const glm::mat4& m);

    bool is_valid() const { return program_ != 0; }
    const std::string& get_error() const { return error_; }

private:
    void destroy();
    GLint uniform_location(std::string_view name);

    GLuint program_ = 0;
    std::string error_;
    std::unordered_map<std::string, GLint> uniform_cache_;
};

// Built-in fullscreen vertex shader (covers the screen with a single triangle).
const char* default_fullscreen_vertex_source();

} // namespace tubelight
