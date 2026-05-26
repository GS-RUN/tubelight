// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN

#pragma once

#include "gl_common.h"

namespace tubelight {

// Draws a fullscreen triangle covering the viewport. Uses the single-triangle
// technique (no VBO data; vertices generated from gl_VertexID) — the only
// state needed is an empty VAO.
class FullscreenQuad {
public:
    FullscreenQuad() = default;
    ~FullscreenQuad();

    FullscreenQuad(const FullscreenQuad&) = delete;
    FullscreenQuad& operator=(const FullscreenQuad&) = delete;

    bool create();
    void destroy();
    void draw() const;

private:
    GLuint vao_ = 0;
};

} // namespace tubelight
