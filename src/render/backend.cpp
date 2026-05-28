// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)

#include "render/backend.h"

#include "render/backend_gl.h"
#if defined(TUBELIGHT_HAVE_D3D12)
#include "render/backend_d3d12.h"
#endif

#include <cstring>

namespace tubelight {

std::unique_ptr<IRenderBackend> create_backend(BackendKind kind) {
    switch (kind) {
        case BackendKind::OpenGL:
            return std::make_unique<GLBackend>();
        case BackendKind::D3D12:
#if defined(TUBELIGHT_HAVE_D3D12)
            return std::make_unique<D3D12Backend>();
#else
            return nullptr;
#endif
    }
    return nullptr;
}

bool parse_backend_kind(const char* token, BackendKind& out) {
    if (!token) return false;
    if (std::strcmp(token, "gl") == 0 || std::strcmp(token, "opengl") == 0) {
        out = BackendKind::OpenGL;
        return true;
    }
    if (std::strcmp(token, "dx12") == 0 || std::strcmp(token, "d3d12") == 0) {
        out = BackendKind::D3D12;
        return true;
    }
    return false;
}

const char* backend_kind_token(BackendKind kind) {
    switch (kind) {
        case BackendKind::OpenGL: return "gl";
        case BackendKind::D3D12:  return "dx12";
    }
    return "?";
}

} // namespace tubelight
