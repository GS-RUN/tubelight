// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)
//
// Phase 3d of ADR-0002 — Windows.Graphics.Capture (WGC) wrapper.
//
// WGC entrega per-window (or per-monitor) captured frames as
// IDirect3D11Texture2D objects via the Direct3D11CaptureFramePool +
// GraphicsCaptureSession + FrameArrived event pattern.
//
// SCOPE Phase 3d (this file): standalone WGC capture that exposes the
// latest captured D3D11 texture. D3D11On12 interop to bind the texture
// to a D3D12 backend lives in render/backend_d3d12.{h,cpp}.
//
// Why WGC over DXGI Desktop Duplication?
//   - WGC entrega per-window texture, less bandwidth than monitor +
//     crop (~40-60% less for typical overlay use).
//   - Better composition path (no extra DWM stall).
//   - Native per-monitor capture without dropping protected content.
//
// Requirements: Windows 10 1903+ for window capture. Compiled with
// MSVC + Windows SDK 10.0.26100 winrt/* headers (no cppwinrt.exe
// codegen needed — Windows SDK includes the projection headers).

#pragma once

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wrl/client.h>
#include <d3d11.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

namespace tubelight {

// Opaque PIMPL — winrt/* headers are heavy and would force every
// consumer to include them. Hide them behind the impl pointer.
class WgcCapture {
public:
    WgcCapture();
    ~WgcCapture();

    WgcCapture(const WgcCapture&) = delete;
    WgcCapture& operator=(const WgcCapture&) = delete;

    // True iff WGC is supported on this Windows build (>= 1903 with
    // the GraphicsCaptureSession::IsSupported() flag set). Cheap to call.
    static bool is_supported();

    // Initialize capture targeting a specific window. The D3D11 device
    // must be shared with whatever will consume the captured textures
    // (e.g. the D3D11On12-wrapped device of a D3D12 backend). Returns
    // false if WGC is unavailable, the HWND isn't valid for capture, or
    // device wrapping fails.
    bool init_for_window(HWND target, ID3D11Device* device);

    // Initialize for full-monitor capture (HMONITOR from MonitorFromXXX).
    bool init_for_monitor(HMONITOR monitor, ID3D11Device* device);

    // Begin pulling frames. The framework calls back internally on each
    // arrived frame and stashes the latest texture (overwriting older).
    bool start();

    // Stop pulling. Safe to call from any thread; idempotent.
    void stop();

    // Returns the latest captured frame as an ID3D11Texture2D. Caller
    // should NOT hold the returned ptr across the next call (the
    // framework may recycle the underlying resource). Returns null if
    // no frame has arrived yet. Thread-safe.
    Microsoft::WRL::ComPtr<ID3D11Texture2D> latest_frame(int& out_w,
                                                          int& out_h);

    // Total frames received since start(). Monotonic, wraps at 2^64.
    uint64_t frame_count() const;

    // True iff init_for_* + start were called and capture is live.
    bool is_running() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tubelight

#endif // _WIN32
