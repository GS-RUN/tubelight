// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)
//
// PipeWire screencast portal — implementation stub.
//
// F6 ships scaffolding: API surface and lifecycle are complete; the actual
// D-Bus calls to org.freedesktop.portal.ScreenCast (CreateSession /
// SelectSources / Start / OpenPipeWireRemote) and PipeWire stream connection
// are deferred to F7 along with the rest of the polish work.
//
// Current behaviour: request_session() and start_stream() report a friendly
// "not yet implemented" error so the rest of the codebase can wire the
// fallback path without crashing.

#include "capture/pipewire_capture.h"

#include <pipewire/pipewire.h>

#include <cstdio>

namespace tubelight::capture {

struct PipeWireCapture::Impl {
    bool initialized = false;
};

PipeWireCapture::PipeWireCapture() : impl_(new Impl()) {
    // Initialize once per process. pw_init is idempotent and safe to call
    // multiple times.
    pw_init(nullptr, nullptr);
    impl_->initialized = true;
}

PipeWireCapture::~PipeWireCapture() {
    if (impl_->initialized) {
        // pw_deinit is also safe; we leave the global init alive because
        // other Tubelight subsystems may share it.
    }
    delete impl_;
}

bool PipeWireCapture::request_session(std::string& error_out) {
    error_out =
        "tubelight::capture::PipeWireCapture::request_session — F6 scaffolding stub. "
        "Portal D-Bus integration is in F7. Until then use the Vulkan layer "
        "or LD_PRELOAD path on Linux.";
    std::fprintf(stderr, "[tubelight-capture] %s\n", error_out.c_str());
    return false;
}

bool PipeWireCapture::start_stream(const FrameCallback& /*on_frame*/, std::string& error_out) {
    error_out = "tubelight::capture::PipeWireCapture::start_stream — F6 scaffolding stub.";
    std::fprintf(stderr, "[tubelight-capture] %s\n", error_out.c_str());
    return false;
}

void PipeWireCapture::stop() {
    // No-op while stub is in place.
}

} // namespace tubelight::capture
