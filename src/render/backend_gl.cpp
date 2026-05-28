// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)

#include "render/backend_gl.h"

#include "core/gl_common.h"

#include <cstdio>

namespace tubelight {

GLBackend::~GLBackend() {
    shutdown();
}

bool GLBackend::init(const BackendInitParams& /*params*/) {
    if (ready_) return true;
    if (!quad_.create()) {
        std::fprintf(stderr, "[tubelight] GLBackend: FullscreenQuad::create failed\n");
        return false;
    }
    ready_ = true;
    return true;
}

void GLBackend::shutdown() {
    if (!ready_) return;
    quad_.destroy();
    ready_ = false;
}

void GLBackend::bind_default_framebuffer() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GLBackend::set_viewport(int x, int y, int w, int h) {
    glViewport(x, y, w, h);
}

void GLBackend::clear_color(float r, float g, float b, float a) {
    glClearColor(r, g, b, a);
    glClear(GL_COLOR_BUFFER_BIT);
}

void GLBackend::draw_fullscreen_quad() {
    quad_.draw();
}

} // namespace tubelight
