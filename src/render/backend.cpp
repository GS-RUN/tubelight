// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)

#include "render/backend.h"

#include "render/backend_gl.h"

#include <cstring>

namespace tubelight {

std::unique_ptr<IRenderBackend> create_backend(BackendKind kind) {
    switch (kind) {
        case BackendKind::OpenGL:
            return std::make_unique<GLBackend>();
    }
    return nullptr;
}

bool parse_backend_kind(const char* token, BackendKind& out) {
    if (!token) return false;
    if (std::strcmp(token, "gl") == 0 || std::strcmp(token, "opengl") == 0) {
        out = BackendKind::OpenGL;
        return true;
    }
    // "dx12" is reserved for Phase 3b — rejected for now so users get a
    // clear error rather than silently falling back to GL.
    return false;
}

const char* backend_kind_token(BackendKind kind) {
    switch (kind) {
        case BackendKind::OpenGL: return "gl";
    }
    return "?";
}

} // namespace tubelight
