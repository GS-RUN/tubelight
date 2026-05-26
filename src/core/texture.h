// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN

#pragma once

#include "gl_common.h"

#include <string>
#include <string_view>

namespace tubelight {

class Texture2D {
public:
    Texture2D() = default;
    ~Texture2D();

    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;
    Texture2D(Texture2D&& other) noexcept;
    Texture2D& operator=(Texture2D&& other) noexcept;

    // Loads a PNG/JPG/BMP from disk via stb_image.
    bool load_from_file(std::string_view path, bool flip_vertical = true);

    // Creates an empty texture (for FBO-style use; the FBO class is the typical caller).
    bool create_empty(int width, int height, GLenum internal_format = GL_RGBA8);

    void destroy();

    void bind(GLenum unit = GL_TEXTURE0) const;

    GLuint id() const { return id_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool is_valid() const { return id_ != 0; }
    const std::string& get_error() const { return error_; }

private:
    GLuint id_      = 0;
    int width_      = 0;
    int height_     = 0;
    std::string error_;
};

} // namespace tubelight
