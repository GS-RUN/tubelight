// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)

#pragma once

#include "gl_common.h"

namespace tubelight {

class FBO {
public:
    FBO() = default;
    ~FBO();

    FBO(const FBO&) = delete;
    FBO& operator=(const FBO&) = delete;
    FBO(FBO&& other) noexcept;
    FBO& operator=(FBO&& other) noexcept;

    // Creates color-only framebuffer of given size and internal format.
    // Default RGBA16F gives HDR-style intermediate storage which is necessary
    // for linear-space passes (bloom, beam) to avoid clipping at 1.0.
    bool create(int width, int height, GLenum internal_format = GL_RGBA16F);

    void resize(int width, int height);
    void destroy();

    void bind() const;
    static void unbind();

    GLuint texture() const { return texture_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool is_valid() const { return fbo_ != 0; }

private:
    GLuint fbo_     = 0;
    GLuint texture_ = 0;
    int width_      = 0;
    int height_     = 0;
    GLenum internal_format_ = GL_RGBA16F;
};

} // namespace tubelight
