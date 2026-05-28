// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)
//
// OpenGL implementation of IRenderBackend. Wraps the existing
// `FullscreenQuad` VAO so Phase 3a is a pure re-routing change with
// zero functional difference vs. v0.1.6.

#pragma once

#include "core/quad.h"
#include "render/backend.h"

namespace tubelight {

class GLBackend final : public IRenderBackend {
public:
    GLBackend() = default;
    ~GLBackend() override;

    GLBackend(const GLBackend&) = delete;
    GLBackend& operator=(const GLBackend&) = delete;

    bool init() override;
    void shutdown() override;
    const char* name() const override { return "OpenGL 4.5 core (libepoxy)"; }

    void bind_default_framebuffer() override;
    void set_viewport(int x, int y, int w, int h) override;
    void clear_color(float r, float g, float b, float a) override;
    void draw_fullscreen_quad() override;

private:
    FullscreenQuad quad_;
    bool ready_ = false;
};

} // namespace tubelight
