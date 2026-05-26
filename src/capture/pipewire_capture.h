// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// PipeWire screencast portal — Linux capture fallback.
//
// When the Vulkan layer / LD_PRELOAD path can't attach (e.g. the target
// runs inside a Flatpak sandbox, anti-cheat blocks the .so, or the user
// just asked for --fallback always), Tubelight asks the desktop session
// for screencast access via xdg-desktop-portal and consumes the resulting
// PipeWire node.

#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace tubelight::capture {

struct FrameView {
    const std::uint8_t* pixels;
    int width;
    int height;
    int stride;
    enum class Format { Rgba8, Bgra8, Rgb8 } format;
};

using FrameCallback = std::function<void(const FrameView&)>;

class PipeWireCapture {
public:
    PipeWireCapture();
    ~PipeWireCapture();

    PipeWireCapture(const PipeWireCapture&) = delete;
    PipeWireCapture& operator=(const PipeWireCapture&) = delete;

    // Requests screencast access via xdg-desktop-portal. Returns false if
    // the portal is unavailable or the user denies access.
    bool request_session(std::string& error_out);

    // Begins receiving frames via the PipeWire stream node. Blocking on the
    // caller's loop; supply a callback that runs on the PipeWire IO thread.
    bool start_stream(const FrameCallback& on_frame, std::string& error_out);

    void stop();

private:
    struct Impl;
    Impl* impl_;
};

} // namespace tubelight::capture
