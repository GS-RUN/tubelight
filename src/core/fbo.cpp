// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)

#include "core/fbo.h"

#include <cstdio>
#include <utility>

namespace tubelight {

FBO::~FBO() {
    destroy();
}

FBO::FBO(FBO&& other) noexcept
    : fbo_(other.fbo_),
      texture_(other.texture_),
      width_(other.width_),
      height_(other.height_),
      internal_format_(other.internal_format_) {
    other.fbo_ = 0;
    other.texture_ = 0;
}

FBO& FBO::operator=(FBO&& other) noexcept {
    if (this != &other) {
        destroy();
        fbo_ = other.fbo_;
        texture_ = other.texture_;
        width_ = other.width_;
        height_ = other.height_;
        internal_format_ = other.internal_format_;
        other.fbo_ = 0;
        other.texture_ = 0;
    }
    return *this;
}

bool FBO::create(int width, int height, GLenum internal_format) {
    destroy();
    width_ = width;
    height_ = height;
    internal_format_ = internal_format;

    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internal_format), width, height,
                 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "[tubelight] FBO::create incomplete (status=0x%x)\n", status);
        destroy();
        return false;
    }
    return true;
}

void FBO::resize(int width, int height) {
    if (width == width_ && height == height_ && fbo_ != 0) {
        return;
    }
    create(width, height, internal_format_);
}

void FBO::destroy() {
    if (fbo_ != 0) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }
    if (texture_ != 0) {
        glDeleteTextures(1, &texture_);
        texture_ = 0;
    }
    width_ = 0;
    height_ = 0;
}

void FBO::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width_, height_);
}

void FBO::unbind() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

} // namespace tubelight
