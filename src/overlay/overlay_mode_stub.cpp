// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)
//
// Non-Windows stub for overlay mode. Replaced by a PipeWire portal
// implementation in v1.1.

#if !defined(_WIN32)

#include "overlay/overlay_mode.h"

#include <cstdio>

namespace tubelight::overlay {

int run(const Options& /*opts*/) {
    std::fprintf(stderr,
        "[overlay] not yet implemented on this platform.\n"
        "  Windows: works via DXGI Desktop Duplication.\n"
        "  Linux:   PipeWire screencast portal lands in v1.1.\n");
    return 2;
}

} // namespace tubelight::overlay

#endif
