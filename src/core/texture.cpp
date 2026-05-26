// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN

#include "core/texture.h"
#include "io/image_io.h"

#include <utility>

namespace tubelight {

Texture2D::~Texture2D() {
    destroy();
}

Texture2D::Texture2D(Texture2D&& other) noexcept
    : id_(other.id_),
      width_(other.width_),
      height_(other.height_),
      error_(std::move(other.error_)) {
    other.id_ = 0;
}

Texture2D& Texture2D::operator=(Texture2D&& other) noexcept {
    if (this != &other) {
        destroy();
        id_ = other.id_;
        width_ = other.width_;
        height_ = other.height_;
        error_ = std::move(other.error_);
        other.id_ = 0;
    }
    return *this;
}

void Texture2D::destroy() {
    if (id_ != 0) {
        glDeleteTextures(1, &id_);
        id_ = 0;
    }
    width_ = 0;
    height_ = 0;
}

bool Texture2D::load_from_file(std::string_view path, bool flip_vertical) {
    destroy();
    error_.clear();

    ImageData image;
    if (!load_image(std::string(path), image, error_, flip_vertical)) {
        return false;
    }

    width_ = image.width;
    height_ = image.height;

    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);

    const GLenum format = (image.channels == 4) ? GL_RGBA
                        : (image.channels == 3) ? GL_RGB
                        : (image.channels == 1) ? GL_RED
                        : GL_RGBA;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, width_, height_,
                 0, format, GL_UNSIGNED_BYTE, image.pixels.data());

    // Nearest-neighbor: we treat source as discrete pixels of the original signal.
    // Linear blending happens inside the shader passes when desired.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    return true;
}

bool Texture2D::create_empty(int width, int height, GLenum internal_format) {
    destroy();
    width_ = width;
    height_ = height;

    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(internal_format), width, height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return true;
}

void Texture2D::bind(GLenum unit) const {
    glActiveTexture(unit);
    glBindTexture(GL_TEXTURE_2D, id_);
}

} // namespace tubelight
