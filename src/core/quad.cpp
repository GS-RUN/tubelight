// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)

#include "core/quad.h"

namespace tubelight {

FullscreenQuad::~FullscreenQuad() {
    destroy();
}

bool FullscreenQuad::create() {
    destroy();
    glGenVertexArrays(1, &vao_);
    return vao_ != 0;
}

void FullscreenQuad::destroy() {
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
}

void FullscreenQuad::draw() const {
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

} // namespace tubelight
