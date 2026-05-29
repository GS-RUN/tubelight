// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)
//
// Windows implementation of the overlay mode.
//
// Strategy (the "no-injection" path; latency ~1 frame, always works):
//   1. Create a fullscreen borderless GLFW window with OpenGL 4.5 core,
//      WS_EX_TOPMOST + WS_EX_NOACTIVATE so the user keeps interacting with
//      whatever is underneath.
//   2. Call SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) so DXGI does
//      NOT see the overlay itself in its captures â€” otherwise we'd grab
//      our own output and feedback.
//   3. Set up DXGI Desktop Duplication on the chosen monitor.
//   4. Each frame: AcquireNextFrame (with 0 ms timeout â€” re-use previous
//      capture if nothing changed), CopyResource into a CPU-readable
//      staging texture, Map, memcpy the BGRA8 bytes into a CPU buffer.
//   5. glTexSubImage2D into the OpenGL source texture; flip BGRAâ†’RGBA via
//      a sampler swizzle (TEXTURE_SWIZZLE_RGBA).
//   6. Run the pipeline; the output fills the full overlay window.
//
// Keys:
//   ESC                quit
//   1..8               toggle individual passes
//   0                  re-enable all passes
//   F                  toggle "freeze" â€” keep last capture
//   space / right      step a quarter-second forward through profile presets
//                       (v1.1; only ESC + numerics today)

#if !defined(_WIN32)
#error "overlay_mode_win.cpp is Windows-only"
#endif

#include "overlay/overlay_mode.h"

#include "core/gl_common.h"
#include "core/pipeline.h"
#include "core/texture.h"
#include "audio/crt_audio.h"
#include "overlay/capture_to_disk.h"
#include "overlay/menu.h"
#include "overlay/preset_saver.h"
#include "overlay/settings.h"
#include "profile/profile_loader.h"

// T5.5 — WGC + D3D11On12 + D3D12 overlay path (run_dx12). Only available
// when the D3D12 backend was compiled in (TUBELIGHT_HAVE_D3D12).
#if defined(TUBELIGHT_HAVE_D3D12)
#include "capture/wgc_capture.h"
#include "render/backend_d3d12.h"
#endif

#ifdef TUBELIGHT_HAS_IMGUI
#include <imgui.h>
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <magnification.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace tubelight::overlay {

// ---------------------------------------------------------------------------
// Global atomics fed by the low-level keyboard hook (callback below).
// ---------------------------------------------------------------------------
namespace {

std::atomic<bool> g_hk_quit{false};
std::atomic<bool> g_hk_freeze_toggle{false};
std::atomic<bool> g_hk_all_on{false};
std::atomic<int>  g_hk_toggle_pass{-1};
std::atomic<bool> g_hk_toggle_menu{false};
std::atomic<bool> g_hk_screenshot{false};
std::atomic<bool> g_hk_toggle_video{false};
std::atomic<bool> g_hk_toggle_fullscreen{false};
std::atomic<bool> g_hk_toggle_target{false};
std::atomic<bool> g_hk_toggle_hud{false};
std::atomic<bool> g_hk_toggle_clickthrough{false}; // kept dynamic until ADR-0001 Phase 1b
std::atomic<bool> g_hk_toggle_recordable{false};   // unused after ADR-0001, never fires
// Stored as atomic for ABI compatibility with consumers (grab_source,
// menu writes, etc.). Per ADR-0001 (2026-05-27) recordable is always
// true at runtime — initialized at startup and never written false.
std::atomic<bool> g_recordable_mode{true};

// WDA_NONE always — the overlay must be visible to external recorders
// (Snipping Tool / Game Bar / OBS) at all times. Feedback prevention is
// handled by routing internal capture through the Magnification API
// with our HWND in MagSetWindowFilterList(MW_FILTERMODE_EXCLUDE, ...),
// which is the only Win32 mechanism that gives per-capturer exclusion
// (WDA is binary across DWM).
inline void apply_capture_affinity(HWND hwnd) {
    (void)hwnd;
    SetWindowDisplayAffinity(hwnd, WDA_NONE);
}

// Track which interesting keys are currently held so we only fire on the
// initial WM_KEYDOWN, never on the (potentially-many) auto-repeats that
// would otherwise toggle the same hotkey several times per press.
static bool s_key_held[256] = {};

LRESULT CALLBACK kb_hook_proc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode != HC_ACTION) return CallNextHookEx(nullptr, nCode, wParam, lParam);
    const auto* kbd = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
    const DWORD vk = kbd->vkCode;

    if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
        if (vk < 256) s_key_held[vk] = false;
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    if (wParam != WM_KEYDOWN && wParam != WM_SYSKEYDOWN) {
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    // Skip auto-repeats.
    if (vk < 256 && s_key_held[vk]) {
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }
    if (vk < 256) s_key_held[vk] = true;

    const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool alt  = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;
    if (ctrl && alt) {
        if (vk == 'Q')                            g_hk_quit = true;
        else if (vk == 'F')                       g_hk_freeze_toggle = true;
        else if (vk == 'M')                       g_hk_toggle_menu = true;
        else if (vk == 'S')                       g_hk_screenshot = true;
        else if (vk == 'V')                       g_hk_toggle_video = true;
        else if (vk == VK_RETURN)                 g_hk_toggle_fullscreen = true;
        else if (vk == 'T')                       g_hk_toggle_target = true;
        else if (vk == 'H')                       g_hk_toggle_hud = true;
        // Ctrl+Alt+C and Ctrl+Alt+R removed per ADR-0001 — recordable
        // and click-through are now always-on, no user toggle.
        else if (vk == '0' || vk == VK_NUMPAD0)   g_hk_all_on = true;
        else if (vk >= '1' && vk <= '8')          g_hk_toggle_pass = static_cast<int>(vk - '1');
        else if (vk >= VK_NUMPAD1 && vk <= VK_NUMPAD8)
                                                 g_hk_toggle_pass = static_cast<int>(vk - VK_NUMPAD1);
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// ---------------------------------------------------------------------------
// WndProc subclass so the overlay keeps rendering DURING a window
// move / resize. Without it, the modal sizing loop Windows enters when the
// user grabs the title bar runs entirely inside DefWindowProc â€” glfwPollEvents
// never returns â€” so the captured frame freezes until the user releases the
// mouse. WM_ENTERSIZEMOVE â†’ start a ~16 ms WM_TIMER that drives one render
// per tick; WM_EXITSIZEMOVE kills it again.
// ---------------------------------------------------------------------------

constexpr UINT_PTR kModalRenderTimerId = 0xCAFEU;
const wchar_t* const kFrameRenderProp  = L"TubelightFrameRenderer";

// Lookup the HWND of a target window by title substring (case-insensitive)
// or by process id. Returns nullptr if nothing matched. We skip invisible
// and zero-size windows, plus our own HWND (passed in so we don't return
// the overlay itself if its title happens to contain the search string).
struct TargetFindCtx {
    std::string title_lower;  // already lowercased
    DWORD       pid;          // 0 = ignore pid filter
    HWND        self;         // exclude from results
    HWND        found;
};

BOOL CALLBACK target_enum_proc(HWND hwnd, LPARAM lp) {
    auto* c = reinterpret_cast<TargetFindCtx*>(lp);
    if (hwnd == c->self) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    RECT r;
    if (!GetWindowRect(hwnd, &r)) return TRUE;
    if (r.right - r.left <= 0 || r.bottom - r.top <= 0) return TRUE;

    if (c->pid != 0) {
        DWORD wpid = 0;
        GetWindowThreadProcessId(hwnd, &wpid);
        if (wpid != c->pid) return TRUE;
    }
    if (!c->title_lower.empty()) {
        char buf[512] = {};
        GetWindowTextA(hwnd, buf, sizeof(buf) - 1);
        std::string title(buf);
        std::transform(title.begin(), title.end(), title.begin(),
                       [](unsigned char ch) { return std::tolower(ch); });
        if (title.find(c->title_lower) == std::string::npos) return TRUE;
    }
    c->found = hwnd;
    return FALSE;  // stop enumeration
}

HWND find_target_hwnd(const std::string& title, int pid, HWND self) {
    std::string title_lower = title;
    std::transform(title_lower.begin(), title_lower.end(), title_lower.begin(),
                   [](unsigned char ch) { return std::tolower(ch); });
    TargetFindCtx ctx{title_lower, static_cast<DWORD>(pid), self, nullptr};
    EnumWindows(&target_enum_proc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

// Returns the client-area rect of `target` mapped to screen coordinates.
// Falls back to GetWindowRect if the client query fails.
bool get_target_screen_rect(HWND target, int& x, int& y, int& w, int& h) {
    if (!target || !IsWindow(target)) return false;
    POINT tl{0, 0};
    RECT cr{};
    if (ClientToScreen(target, &tl) && GetClientRect(target, &cr)) {
        int cw = cr.right - cr.left;
        int ch = cr.bottom - cr.top;
        if (cw > 0 && ch > 0) {
            x = tl.x; y = tl.y; w = cw; h = ch;
            return true;
        }
    }
    RECT wr;
    if (GetWindowRect(target, &wr)) {
        x = wr.left; y = wr.top;
        w = wr.right - wr.left; h = wr.bottom - wr.top;
        return (w > 0 && h > 0);
    }
    return false;
}

struct FrameRenderState {
    std::function<void()> render_one;
};

WNDPROC g_orig_wndproc = nullptr;

// Flag for the subclass proc to suppress the non-client area entirely
// (title bar + borders â†’ 0 pixels). Used by the runtime fullscreen toggle
// to go borderless WITHOUT touching GWL_STYLE / WS_EX_LAYERED â€” those style
// swaps drop WDA_EXCLUDEFROMCAPTURE on this hardware (NVIDIA + Win11), which
// in turn breaks DXGI Desktop Duplication's exclusion of our own overlay
// and produces the recursive feedback ghost effect.
std::atomic<bool> g_hide_nonclient{false};

// When true, the subclass proc returns HTTRANSPARENT for every hit-test
// query, causing Windows to deliver mouse events to whatever's underneath
// our overlay (i.e. true click-through) WITHOUT requiring WS_EX_LAYERED
// or WS_EX_TRANSPARENT â€” those have to interact with OpenGL composition
// in fiddly ways. NCHITTEST is the clean low-level path: independent of
// style bits, takes effect on the very next click.
std::atomic<bool> g_clickthrough_effective{false};

LRESULT CALLBACK tubelight_subclass_proc(HWND hwnd, UINT msg,
                                         WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCHITTEST:
        if (g_clickthrough_effective.load()) {
            // Tell Windows "this pixel is transparent" â†’ the click is
            // re-routed to whatever window is behind us in z-order.
            // Works without WS_EX_LAYERED, doesn't fight OpenGL.
            static std::atomic<int> log_once{0};
            if (log_once.exchange(1) == 0) {
                std::fprintf(stderr, "[overlay] WM_NCHITTEST â†’ HTTRANSPARENT (click-through active)\n");
            }
            return HTTRANSPARENT;
        }
        break;
    case WM_MOUSEACTIVATE:
        if (g_clickthrough_effective.load()) {
            return MA_NOACTIVATEANDEAT;
        }
        break;
    // Ctrl+Alt+C WndProc fallback removed per ADR-0001 — click-through
    // is always-on now, no user toggle to fall back to.
    case WM_NCCALCSIZE:
        // wp == TRUE means lp points to NCCALCSIZE_PARAMS and we're asked
        // for the *new* client rect. Returning 0 with rgrc[0] left at the
        // proposed full window rect tells Windows: client == window, no
        // non-client area. The window keeps WS_OVERLAPPEDWINDOW + caption
        // styles, but they render as zero-height.
        if (wp == TRUE && g_hide_nonclient.load()) {
            return 0;
        }
        break;
    case WM_ENTERSIZEMOVE:
        SetTimer(hwnd, kModalRenderTimerId, USER_TIMER_MINIMUM, nullptr);
        break;
    case WM_EXITSIZEMOVE:
        KillTimer(hwnd, kModalRenderTimerId);
        break;
    case WM_TIMER:
        if (wp == kModalRenderTimerId) {
            auto* fr = reinterpret_cast<FrameRenderState*>(
                GetPropW(hwnd, kFrameRenderProp));
            if (fr && fr->render_one) fr->render_one();
            return 0;
        }
        break;
    }
    return CallWindowProcW(g_orig_wndproc, hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// DXGI Desktop Duplication capture
// ---------------------------------------------------------------------------

class DxgiCapture {
public:
    bool init(int monitor_index) {
        // 1) D3D11 device.
        D3D_FEATURE_LEVEL fl;
        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            0, nullptr, 0, D3D11_SDK_VERSION,
            &device_, &fl, &context_);
        if (FAILED(hr)) {
            std::fprintf(stderr, "[overlay] D3D11CreateDevice failed: 0x%08lx\n", hr);
            return false;
        }

        // 2) Enumerate adapter â†’ output â†’ duplication.
        ComPtr<IDXGIDevice> dxgi_device;
        device_.As(&dxgi_device);
        ComPtr<IDXGIAdapter> adapter;
        dxgi_device->GetAdapter(&adapter);

        ComPtr<IDXGIOutput> output;
        if (FAILED(adapter->EnumOutputs(static_cast<UINT>(monitor_index), &output))) {
            std::fprintf(stderr, "[overlay] EnumOutputs(%d) failed; falling back to monitor 0\n",
                         monitor_index);
            if (FAILED(adapter->EnumOutputs(0, &output))) {
                return false;
            }
        }
        ComPtr<IDXGIOutput1> output1;
        if (FAILED(output.As(&output1))) {
            std::fprintf(stderr, "[overlay] output does not support IDXGIOutput1\n");
            return false;
        }

        DXGI_OUTPUT_DESC odesc;
        output1->GetDesc(&odesc);
        width_  = odesc.DesktopCoordinates.right  - odesc.DesktopCoordinates.left;
        height_ = odesc.DesktopCoordinates.bottom - odesc.DesktopCoordinates.top;
        origin_x_ = odesc.DesktopCoordinates.left;
        origin_y_ = odesc.DesktopCoordinates.top;

        hr = output1->DuplicateOutput(device_.Get(), &dup_);
        if (FAILED(hr)) {
            std::fprintf(stderr, "[overlay] DuplicateOutput failed: 0x%08lx\n", hr);
            return false;
        }

        // 3) Staging texture (CPU-readable BGRA8 of monitor size).
        D3D11_TEXTURE2D_DESC td = {};
        td.Width  = static_cast<UINT>(width_);
        td.Height = static_cast<UINT>(height_);
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_STAGING;
        td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        td.BindFlags = 0;
        if (FAILED(device_->CreateTexture2D(&td, nullptr, &staging_))) {
            std::fprintf(stderr, "[overlay] staging texture create failed\n");
            return false;
        }

        cpu_buffer_.resize(static_cast<size_t>(width_) * height_ * 4);
        monitor_index_ = monitor_index;
        std::fprintf(stderr, "[overlay] DXGI duplication ready: %dx%d at (%d,%d)\n",
                     width_, height_, origin_x_, origin_y_);
        return true;
    }

    // Captures the next frame. If no new frame is available within the
    // timeout, returns true with new_frame=false (use previous capture).
    // ACCESS_LOST is treated as recoverable (re-init transparently).
    // Returns false only on terminal failures.
    bool grab(bool& new_frame, DWORD timeout_ms = 16) {
        new_frame = false;
        if (!dup_) return false;

        DXGI_OUTDUPL_FRAME_INFO info = {};
        ComPtr<IDXGIResource> res;
        HRESULT hr = dup_->AcquireNextFrame(timeout_ms, &info, &res);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            return true; // not new, not an error
        }
        if (hr == DXGI_ERROR_ACCESS_LOST) {
            // Common right after creating a topmost window. Recover by
            // re-creating the duplication on the same output.
            std::fprintf(stderr, "[overlay] ACCESS_LOST â€” recreating duplication...\n");
            dup_.Reset();
            return reacquire_duplication();
        }
        if (FAILED(hr)) {
            std::fprintf(stderr, "[overlay] AcquireNextFrame failed: 0x%08lx\n", hr);
            return false;
        }

        // Latency win: AcquireNextFrame returns success even when only
        // the mouse cursor moved (no desktop pixel change). LastPresentTime
        // is 0 in that case. Skip CopyResource + Map + memcpy entirely
        // when there's no real content update — saves ~2-5 ms per frame
        // when the desktop is idle, and avoids unnecessarily re-uploading
        // identical pixels to the GL source texture.
        if (info.LastPresentTime.QuadPart == 0) {
            dup_->ReleaseFrame();
            return true; // new_frame stays false, caller re-uses last
        }

        ComPtr<ID3D11Texture2D> frame_tex;
        res.As(&frame_tex);

        // Copy desktop -> staging (GPU-side).
        context_->CopyResource(staging_.Get(), frame_tex.Get());

        // Map staging and copy to CPU buffer.
        D3D11_MAPPED_SUBRESOURCE map = {};
        if (SUCCEEDED(context_->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &map))) {
            const auto* src = static_cast<const uint8_t*>(map.pData);
            uint8_t* dst = cpu_buffer_.data();
            const size_t row_bytes = static_cast<size_t>(width_) * 4;
            for (int y = 0; y < height_; ++y) {
                std::memcpy(dst + y * row_bytes, src + y * map.RowPitch, row_bytes);
            }
            context_->Unmap(staging_.Get(), 0);
            new_frame = true;
        }

        dup_->ReleaseFrame();
        return true;
    }

    void shutdown() {
        dup_.Reset();
        staging_.Reset();
        context_.Reset();
        device_.Reset();
    }

    bool reacquire_duplication() {
        // Walks adapter â†’ output again and creates a fresh duplication.
        ComPtr<IDXGIDevice> dxgi_device;
        if (FAILED(device_.As(&dxgi_device))) return false;
        ComPtr<IDXGIAdapter> adapter;
        if (FAILED(dxgi_device->GetAdapter(&adapter))) return false;
        ComPtr<IDXGIOutput> output;
        if (FAILED(adapter->EnumOutputs(static_cast<UINT>(monitor_index_), &output))) {
            if (FAILED(adapter->EnumOutputs(0, &output))) return false;
        }
        ComPtr<IDXGIOutput1> out1;
        if (FAILED(output.As(&out1))) return false;
        HRESULT hr = out1->DuplicateOutput(device_.Get(), &dup_);
        if (FAILED(hr)) {
            std::fprintf(stderr, "[overlay] re-DuplicateOutput failed: 0x%08lx\n", hr);
            return false;
        }
        return true;
    }

    int width()    const { return width_; }
    int height()   const { return height_; }
    int origin_x() const { return origin_x_; }
    int origin_y() const { return origin_y_; }
    const uint8_t* pixels() const { return cpu_buffer_.data(); }

private:
    ComPtr<ID3D11Device>            device_;
    ComPtr<ID3D11DeviceContext>     context_;
    ComPtr<IDXGIOutputDuplication>  dup_;
    ComPtr<ID3D11Texture2D>         staging_;
    std::vector<uint8_t>            cpu_buffer_;
    int width_ = 0, height_ = 0;
    int origin_x_ = 0, origin_y_ = 0;
    int monitor_index_ = 0;

public:
    // Used by the Magnification-API fallback: it writes its captured
    // BGRA bitmap into our cpu_buffer_ so existing call sites that read
    // capture.pixels() work transparently whichever backend is active.
    std::vector<uint8_t>& mutable_cpu_buffer() { return cpu_buffer_; }
};

// ---------------------------------------------------------------------------
// MagCapture â€” Magnification-API-based source capture
//
// Why this exists: when "recordable mode" is on, we drop
// WDA_EXCLUDEFROMCAPTURE from the overlay so Snipping Tool / Game Bar /
// OBS can record it. But DXGI Desktop Duplication then also sees the
// overlay â†’ CRT pipeline ingests its own output â†’ brightness collapses
// to black within a few frames (each shader pass attenuates). WDA is
// binary across DWM; there is no API to tell DWM "exclude window X from
// THIS capturer". The Magnification API is the only Win32 mechanism
// that does provide per-capturer exclusion via MagSetWindowFilterList,
// so we use it as the internal source while recordable is on. External
// recorders go through DWM composition (which sees our overlay
// normally) and capture us untouched.
//
// Lifecycle:
//   - MagInitialize() once, on the GLFW thread (must match the thread
//     that owns the magnifier window).
//   - A host top-level window holds the magnifier child. The host is
//     visible but WS_EX_LAYERED + alpha=0 â†’ effectively invisible.
//   - MagSetWindowFilterList(MW_FILTERMODE_EXCLUDE, [overlay_hwnd]) is
//     the load-bearing call.
//   - MagSetImageScalingCallback receives the source bitmap each time
//     the magnifier paints (we trigger one paint per grab via
//     InvalidateRect + UpdateWindow on the magnifier child).
//   - Format is BGRA8 top-down, same as DxgiCapture's CPU buffer.
//   - MagSetImageScalingCallback is documented as "deprecated" since
//     Win10 but remains functional in Win11; if Microsoft ever pulls
//     it, we'd migrate to GetMagnifierAPIFrame or whatever replacement
//     ships. As of Win11 26200 it works.
// ---------------------------------------------------------------------------

class MagCapture {
public:
    ~MagCapture() {
        // Destructor must call shutdown() to deregister the scaling
        // callback and clear s_instance. Without this, when the owning
        // unique_ptr destroys MagCapture, any queued WM_PAINT on the
        // magnifier window dispatches scaling_callback into freed memory
        // (use-after-free). Verified PLAUSIBLE in code review.
        if (initialized_ || hwnd_mag_ || hwnd_host_) {
            shutdown();
        }
    }

    bool init(int monitor_index, HWND overlay_to_exclude, HINSTANCE hinst) {
        if (initialized_) return true;
        if (!MagInitialize()) {
            std::fprintf(stderr, "[overlay] MagInitialize failed: GLE=%lu\n",
                         GetLastError());
            return false;
        }
        // Find the monitor rect â€” best-effort enumeration of HMONITORs.
        // For monitor_index == 0 we use the primary; otherwise fall back
        // to EnumDisplayMonitors. Tubelight currently only ever uses 0
        // in practice but keep this consistent with DxgiCapture's API.
        RECT mon_rect;
        if (!find_monitor_rect(monitor_index, mon_rect)) {
            // Fallback to virtual screen if EnumDisplayMonitors fails.
            mon_rect.left   = GetSystemMetrics(SM_XVIRTUALSCREEN);
            mon_rect.top    = GetSystemMetrics(SM_YVIRTUALSCREEN);
            mon_rect.right  = mon_rect.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
            mon_rect.bottom = mon_rect.top  + GetSystemMetrics(SM_CYVIRTUALSCREEN);
        }
        width_    = mon_rect.right  - mon_rect.left;
        height_   = mon_rect.bottom - mon_rect.top;
        origin_x_ = mon_rect.left;
        origin_y_ = mon_rect.top;

        // Register a unique host window class once per process.
        WNDCLASSEXW wc = {};
        wc.cbSize        = sizeof(WNDCLASSEXW);
        wc.lpfnWndProc   = DefWindowProcW;
        wc.hInstance     = hinst;
        wc.lpszClassName = L"TubelightMagHost";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassExW(&wc); // ignore "class already exists" on re-init

        // Host: invisible (alpha 0), topmost, click-through, no-activate.
        // Sized to the monitor so the magnifier child has somewhere to
        // paint at 1:1. Position = monitor origin.
        hwnd_host_ = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT |
            WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            L"TubelightMagHost", L"Tubelight Mag Host",
            WS_POPUP,
            origin_x_, origin_y_, width_, height_,
            nullptr, nullptr, hinst, nullptr);
        if (!hwnd_host_) {
            std::fprintf(stderr,
                "[overlay] mag host CreateWindow failed: GLE=%lu\n",
                GetLastError());
            MagUninitialize();
            return false;
        }
        // alpha=0 â†’ fully transparent. DWM still composes it (so the
        // magnifier paints), but nothing visible reaches the user or
        // external capturers.
        SetLayeredWindowAttributes(hwnd_host_, 0, 0, LWA_ALPHA);

        // Magnifier child. WC_MAGNIFIER is registered by MagInitialize.
        hwnd_mag_ = CreateWindowW(
            WC_MAGNIFIERW, L"MagnifierChild",
            WS_CHILD | WS_VISIBLE,
            0, 0, width_, height_,
            hwnd_host_, nullptr, hinst, nullptr);
        if (!hwnd_mag_) {
            std::fprintf(stderr,
                "[overlay] WC_MAGNIFIER CreateWindow failed: GLE=%lu\n",
                GetLastError());
            DestroyWindow(hwnd_host_); hwnd_host_ = nullptr;
            MagUninitialize();
            return false;
        }

        // CRITICAL ORDER: resize cpu_buffer_ BEFORE installing the
        // scaling callback. Otherwise, if ShowWindow / MagSetWindowSource
        // dispatches a synchronous WM_PAINT (which Win11 can), the
        // callback fires with cpu_buffer_ still a zero-capacity vector
        // and memcpy stomps the heap. Confirmed by code review.
        cpu_buffer_.resize(static_cast<size_t>(width_) * height_ * 4, 0);

        // Filter list stored as a member so the array's address remains
        // valid past init() â€” MSDN does not document whether
        // MagSetWindowFilterList copies or retains the pointer, so we
        // play safe. (We only ever filter one HWND, ours.)
        filter_hwnds_[0] = overlay_to_exclude;
        if (!MagSetWindowFilterList(hwnd_mag_, MW_FILTERMODE_EXCLUDE, 1,
                                     filter_hwnds_)) {
            std::fprintf(stderr,
                "[overlay] MagSetWindowFilterList failed: GLE=%lu\n",
                GetLastError());
        }

        // Install the image-scaling callback. We rely on the static
        // s_instance pointer because Magnification API doesn't pass a
        // user-data parameter to the callback. Only one MagCapture is
        // expected to be live per process â€” assert this.
        if (s_instance != nullptr) {
            std::fprintf(stderr,
                "[overlay] MagCapture: second instance, refusing\n");
            DestroyWindow(hwnd_mag_);  hwnd_mag_  = nullptr;
            DestroyWindow(hwnd_host_); hwnd_host_ = nullptr;
            MagUninitialize();
            return false;
        }
        s_instance = this;
        if (!MagSetImageScalingCallback(hwnd_mag_, &MagCapture::scaling_callback)) {
            std::fprintf(stderr,
                "[overlay] MagSetImageScalingCallback failed: GLE=%lu\n",
                GetLastError());
        }

        // Source rect = full monitor (screen coords).
        src_rect_ = mon_rect;
        MagSetWindowSource(hwnd_mag_, src_rect_);

        // Show the host (with alpha=0). The magnifier child only paints
        // if its window is visible, and that requires the host to be
        // visible. SW_SHOWNA = don't activate (don't steal focus).
        ShowWindow(hwnd_host_, SW_SHOWNA);

        initialized_ = true;
        std::fprintf(stderr,
            "[overlay] Magnification API ready: %dx%d at (%d,%d), "
            "excluding overlay HWND %p\n",
            width_, height_, origin_x_, origin_y_,
            static_cast<void*>(overlay_to_exclude));
        return true;
    }

    void shutdown() {
        if (hwnd_mag_) {
            MagSetImageScalingCallback(hwnd_mag_, nullptr);
            DestroyWindow(hwnd_mag_);
            hwnd_mag_ = nullptr;
        }
        if (hwnd_host_) {
            DestroyWindow(hwnd_host_);
            hwnd_host_ = nullptr;
        }
        if (initialized_) {
            MagUninitialize();
            initialized_ = false;
        }
        s_instance = nullptr;
    }

    // Triggers one paint cycle on the magnifier; the scaling callback
    // fires synchronously inside UpdateWindow and writes into cpu_buffer_.
    bool grab(bool& new_frame) {
        if (!initialized_) { new_frame = false; return false; }
        callback_fired_ = false;
        // Re-asserting the source rect each frame both forces a refresh
        // and is the documented way to drive a fresh capture.
        MagSetWindowSource(hwnd_mag_, src_rect_);
        InvalidateRect(hwnd_mag_, nullptr, FALSE);
        UpdateWindow(hwnd_mag_);
        new_frame = callback_fired_;
        return true;
    }

    int width()    const { return width_; }
    int height()   const { return height_; }
    int origin_x() const { return origin_x_; }
    int origin_y() const { return origin_y_; }
    const uint8_t* pixels() const { return cpu_buffer_.data(); }
    bool is_initialized() const { return initialized_; }

    // Update the source rect that the Mag callback reads from. Fix for
    // ADR-0001 §3: prior to this, src_rect_ was set once in init() to
    // the full monitor and never updated, so target/region attaches at
    // runtime ended up capturing the whole screen instead of the new
    // target area. Now do_attach_target() / do_attach_region() /
    // do_toggle_fullscreen() each call this with their new rect.
    //
    // The rect is in SCREEN coordinates. Also updates the internal
    // width/height/origin so callers (CPU buffer mirror, GL texture
    // upload) see the new dimensions.
    void set_source_rect(int x, int y, int w, int h) {
        if (!initialized_ || w <= 0 || h <= 0) return;
        src_rect_.left   = x;
        src_rect_.top    = y;
        src_rect_.right  = x + w;
        src_rect_.bottom = y + h;
        // Resize the magnifier child so the scaling output matches.
        if (hwnd_mag_) {
            SetWindowPos(hwnd_mag_, nullptr, 0, 0, w, h,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        width_    = w;
        height_   = h;
        origin_x_ = x;
        origin_y_ = y;
        // cpu_buffer_ must hold W*H*4 bytes (BGRA). Resize defensively.
        const size_t need = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
        if (cpu_buffer_.size() < need) {
            cpu_buffer_.resize(need);
        }
        MagSetWindowSource(hwnd_mag_, src_rect_);
        std::fprintf(stderr,
            "[overlay] Mag source rect updated: %dx%d at (%d,%d)\n",
            w, h, x, y);
    }

private:
    static BOOL CALLBACK scaling_callback(HWND /*hwnd*/, void* srcdata,
                                          MAGIMAGEHEADER srcheader,
                                          void* /*destdata*/,
                                          MAGIMAGEHEADER /*destheader*/,
                                          RECT /*unclipped*/,
                                          RECT /*clipped*/,
                                          HRGN /*dirty*/) {
        if (!s_instance || !srcdata) return TRUE;
        const size_t expected =
            static_cast<size_t>(s_instance->width_) *
            static_cast<size_t>(s_instance->height_) * 4;
        // Defensive: refuse to write if the destination buffer is
        // smaller than expected. Without this guard, a synchronous
        // paint during init (before resize completes) â€” or any future
        // race between cpu_buffer_.resize and callback dispatch â€”
        // would memcpy into a too-small vector â†’ heap corruption.
        if (s_instance->cpu_buffer_.size() < expected) return TRUE;
        // srcheader.cbSize is the byte count of the source bitmap.
        // For BGRA8 top-down it matches width*height*4 when the bitmap
        // dimensions match our request.
        if (srcheader.width == static_cast<UINT>(s_instance->width_) &&
            srcheader.height == static_cast<UINT>(s_instance->height_)) {
            std::memcpy(s_instance->cpu_buffer_.data(), srcdata,
                        std::min<size_t>(srcheader.cbSize, expected));
            s_instance->callback_fired_ = true;
        }
        return TRUE;
    }

    struct EnumCtx { int wanted; int current; RECT out; bool found; };
    static BOOL CALLBACK monitor_enum_proc(HMONITOR hmon, HDC, LPRECT, LPARAM lp) {
        auto* c = reinterpret_cast<EnumCtx*>(lp);
        if (c->current == c->wanted) {
            MONITORINFO mi = { sizeof(MONITORINFO) };
            if (GetMonitorInfoW(hmon, &mi)) {
                c->out   = mi.rcMonitor;
                c->found = true;
            }
            return FALSE;
        }
        ++c->current;
        return TRUE;
    }
    static bool find_monitor_rect(int index, RECT& out) {
        EnumCtx ctx{ index, 0, {}, false };
        EnumDisplayMonitors(nullptr, nullptr, &monitor_enum_proc,
                            reinterpret_cast<LPARAM>(&ctx));
        if (ctx.found) { out = ctx.out; return true; }
        return false;
    }

    HWND hwnd_host_ = nullptr;
    HWND hwnd_mag_  = nullptr;
    RECT src_rect_  = {};
    std::vector<uint8_t> cpu_buffer_;
    HWND filter_hwnds_[1] = { nullptr }; // member so the pointer passed
                                          // to MagSetWindowFilterList stays
                                          // valid past init().
    int  width_ = 0, height_ = 0;
    int  origin_x_ = 0, origin_y_ = 0;
    bool initialized_ = false;
    bool callback_fired_ = false;
    static MagCapture* s_instance;
};

MagCapture* MagCapture::s_instance = nullptr;

// Dispatcher: while g_recordable_mode is true and mag has initialised
// successfully, drive grabs through the Mag backend and mirror its
// pixel buffer into the DxgiCapture's cpu_buffer_ so existing
// upload_subregion_to_source() / pixel-read call sites stay unchanged.
// Otherwise route through DXGI Desktop Duplication as before.
bool grab_source(DxgiCapture& dxgi, MagCapture& mag,
                 bool& new_frame, DWORD timeout_ms) {
    if (g_recordable_mode.load() && mag.is_initialized()) {
        bool ok = mag.grab(new_frame);
        if (ok && new_frame) {
            // Mirror Mag pixels into DXGI's CPU buffer.
            auto& dst = dxgi.mutable_cpu_buffer();
            const size_t n = static_cast<size_t>(mag.width()) *
                              static_cast<size_t>(mag.height()) * 4;
            if (dst.size() >= n) {
                std::memcpy(dst.data(), mag.pixels(), n);
            }
        }
        return ok;
    }
    return dxgi.grab(new_frame, timeout_ms);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void glfw_error_cb(int code, const char* msg) {
    std::fprintf(stderr, "[overlay][glfw %d] %s\n", code, msg);
}

struct AppState {
    tubelight::Pipeline* pipeline = nullptr;
    bool freeze = false;
    void* resize_state = nullptr;
};

// Extracts the rect (mon_x, mon_y, mon_w, mon_h) â€” relative to monitor 0,0
// â€” from the DXGI capture and uploads it to `source`, vertically flipped
// (DXGI top-down â†’ GL bottom-up). For fullscreen mode the rect is ignored
// and the full desktop is uploaded.
//
// Out-of-bounds pixels (the user dragged the window partially off-screen)
// are filled with black so the shader sees clean borders.
void upload_subregion_to_source(class DxgiCapture& capture,
                                 tubelight::Texture2D& source,
                                 int mon_x, int mon_y,
                                 int /*mon_w*/, int /*mon_h*/,
                                 int tex_w, int tex_h,
                                 std::vector<uint8_t>& tmp,
                                 bool fullscreen) {
    const uint8_t* src = capture.pixels();
    const int CW = capture.width();
    const int CH = capture.height();
    const size_t row_bytes = static_cast<size_t>(tex_w) * 4;

    if (fullscreen) {
        // Identity path: full desktop, top-down â†’ bottom-up.
        for (int y = 0; y < tex_h; ++y) {
            std::memcpy(tmp.data() + (tex_h - 1 - y) * row_bytes,
                        src + y * static_cast<size_t>(tex_w) * 4, row_bytes);
        }
    } else {
        std::fill(tmp.begin(), tmp.end(), 0);
        for (int y = 0; y < tex_h; ++y) {
            int sy = mon_y + y;
            if (sy < 0 || sy >= CH) continue;
            int dy = tex_h - 1 - y;
            uint8_t* dst_row = tmp.data() + dy * row_bytes;
            // Source row starts at (mon_x, sy). Skip if completely out of bounds.
            int start_x = std::max(0, mon_x);
            int end_x   = std::min(CW, mon_x + tex_w);
            if (end_x <= start_x) continue;
            const uint8_t* src_row = src + (static_cast<size_t>(sy) * CW + start_x) * 4;
            uint8_t* dst_offset = dst_row + (start_x - mon_x) * 4;
            std::memcpy(dst_offset, src_row, static_cast<size_t>(end_x - start_x) * 4);
        }
    }

    glBindTexture(GL_TEXTURE_2D, source.id());
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, tex_w, tex_h,
                    GL_BGRA, GL_UNSIGNED_BYTE, tmp.data());
}

void key_cb(GLFWwindow* w, int key, int /*sc*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS) return;
    auto* s = static_cast<AppState*>(glfwGetWindowUserPointer(w));
    if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(w, GLFW_TRUE);
    if (!s || !s->pipeline) return;
    if (key >= GLFW_KEY_1 && key <= GLFW_KEY_8) {
        int idx = key - GLFW_KEY_1;
        bool cur = s->pipeline->is_pass_enabled(idx);
        s->pipeline->set_pass_enabled(idx, !cur);
        std::printf("[overlay] %s: %s\n",
                    tubelight::pass_display_name(idx), !cur ? "ON" : "OFF");
    } else if (key == GLFW_KEY_0) {
        for (int i = 0; i < tubelight::Pipeline::kPassCount; ++i)
            s->pipeline->set_pass_enabled(i, true);
        std::printf("[overlay] all passes ON\n");
    } else if (key == GLFW_KEY_F) {
        s->freeze = !s->freeze;
        std::printf("[overlay] freeze: %s\n", s->freeze ? "ON" : "OFF");
    }
}

// Phase 3e end-to-end: report the per-frame CPU cost of the capture→GPU
// step (the ShaderGlass differentiator — GL DXGI memcpy+upload vs DX12 WGC
// zero-copy). stderr, since the GL path's stdout can be swallowed.
void capture_bench_report(const char* label, std::vector<double>& cap_ms) {
    if (cap_ms.empty()) {
        std::fprintf(stderr, "[capbench] %-4s: no samples\n", label);
        return;
    }
    std::sort(cap_ms.begin(), cap_ms.end());
    const size_t n = cap_ms.size();
    double sum = 0.0;
    for (double v : cap_ms) sum += v;
    const double avg = sum / static_cast<double>(n);
    std::fprintf(stderr,
        "[capbench] %-4s capture->GPU/frame: avg %.4f ms | p50 %.4f | "
        "p99 %.4f | min %.4f | max %.4f ms (%zu frames)\n",
        label, avg, cap_ms[n / 2],
        cap_ms[std::min(n - 1, static_cast<size_t>(n * 0.99))],
        cap_ms.front(), cap_ms.back(), n);
    std::fflush(stderr);
}

#if defined(TUBELIGHT_HAVE_D3D12)
// ---------------------------------------------------------------------------
// T5.5 — D3D12 + WGC overlay path.
//
// Distinct from the legacy GL run() above: instead of DXGI Desktop
// Duplication → CPU staging → glTexSubImage2D, this captures via
// Windows.Graphics.Capture (per-window or per-monitor), keeps the frame
// as an ID3D11Texture2D, unwraps it back to a D3D12 resource through
// D3D11On12 (zero CPU copy), and samples it directly in the D3D12-driven
// 8-pass Pipeline. Mirrors the loop validated in main.cpp::run_wgc_test.
//
// Scope for v0.2.0-rc.0 (deferred to v0.2.1, see debrief):
//   - ImGui menu stays GL-only → not shown in this path.
//   - Click-through / non-activating overlay → window is focusable so ESC
//     + 1..8 / 0 / F hotkeys work through GLFW.
//   - Region/Windowed capture uses the whole monitor (WGC has monitor
//     granularity; a true crop needs a copy/UV pass).
//   - Target-window position tracking → window is placed once at launch.
// ---------------------------------------------------------------------------

struct MonitorPick {
    HMONITOR hmon = nullptr;
    int x = 0, y = 0, w = 0, h = 0;
    bool ok = false;
};

// Pick the N-th monitor (in EnumDisplayMonitors order) and return its
// HMONITOR + virtual-desktop rect. Falls back to the primary monitor if
// `index` is out of range.
MonitorPick pick_monitor(int index) {
    struct Ctx { int want; int seen; MonitorPick out; } ctx{index, 0, {}};
    EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR hm, HDC, LPRECT, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            if (c->seen == c->want) {
                MONITORINFO mi{}; mi.cbSize = sizeof(mi);
                if (GetMonitorInfoW(hm, &mi)) {
                    c->out.hmon = hm;
                    c->out.x = mi.rcMonitor.left;
                    c->out.y = mi.rcMonitor.top;
                    c->out.w = mi.rcMonitor.right  - mi.rcMonitor.left;
                    c->out.h = mi.rcMonitor.bottom - mi.rcMonitor.top;
                    c->out.ok = true;
                }
            }
            ++c->seen;
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    if (!ctx.out.ok) {
        HMONITOR pm = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{}; mi.cbSize = sizeof(mi);
        GetMonitorInfoW(pm, &mi);
        ctx.out.hmon = pm;
        ctx.out.x = mi.rcMonitor.left;
        ctx.out.y = mi.rcMonitor.top;
        ctx.out.w = mi.rcMonitor.right  - mi.rcMonitor.left;
        ctx.out.h = mi.rcMonitor.bottom - mi.rcMonitor.top;
        ctx.out.ok = true;
    }
    return ctx.out;
}

int run_dx12(const Options& opts) {
    if (!tubelight::WgcCapture::is_supported()) {
        std::fprintf(stderr,
            "[overlay] WGC unsupported (requires Windows 10 1903+); "
            "--renderer dx12 overlay unavailable.\n");
        return 1;
    }
    glfwSetErrorCallback(glfw_error_cb);
    if (!glfwInit()) {
        std::fprintf(stderr, "[overlay] glfwInit failed\n");
        return 1;
    }

    const bool mode_target     = (opts.mode == OverlayMode::TargetWindow);
    const bool mode_fullscreen = (opts.mode == OverlayMode::Fullscreen);
    const bool mode_region     = (opts.mode == OverlayMode::Region);
    const bool mode_windowed   = (opts.mode == OverlayMode::Windowed);

    // Resolve the WGC capture target + capture geometry.
    HWND target_hwnd = nullptr;
    int tgt_x = 0, tgt_y = 0, tgt_w = 0, tgt_h = 0;
    MonitorPick mon{};
    if (mode_target) {
        target_hwnd = find_target_hwnd(opts.target_window, opts.target_pid, nullptr);
        if (!target_hwnd) {
            std::fprintf(stderr,
                "[overlay] target window not found (title='%s' pid=%d)\n",
                opts.target_window.c_str(), opts.target_pid);
            glfwTerminate();
            return 1;
        }
        get_target_screen_rect(target_hwnd, tgt_x, tgt_y, tgt_w, tgt_h);
    } else {
        mon = pick_monitor(opts.monitor_index);
    }

    // Overlay window geometry.
    int win_x, win_y, W, H;
    if (mode_target) {
        win_x = tgt_x; win_y = tgt_y; W = tgt_w; H = tgt_h;
    } else if (mode_fullscreen) {
        win_x = mon.x; win_y = mon.y; W = mon.w; H = mon.h;
    } else if (mode_region) {
        win_x = mon.x + opts.region_x; win_y = mon.y + opts.region_y;
        W = opts.region_w > 0 ? opts.region_w : opts.init_w;
        H = opts.region_h > 0 ? opts.region_h : opts.init_h;
        std::fprintf(stderr,
            "[overlay] dx12: region capture uses full-monitor source "
            "(crop deferred to v0.2.1)\n");
    } else { // windowed
        W = opts.init_w; H = opts.init_h;
        win_x = mon.x + (mon.w - W) / 2;
        win_y = mon.y + (mon.h - H) / 2;
    }

    const bool chrome = mode_windowed;  // only plain windowed gets a title bar
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);   // D3D12 owns the swap chain
    glfwWindowHint(GLFW_VISIBLE,    GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED,  chrome ? GLFW_TRUE  : GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING,   GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE,  chrome ? GLFW_TRUE  : GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(W, H, "Tubelight (DX12)", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "[overlay] glfwCreateWindow failed\n");
        glfwTerminate();
        return 1;
    }
    glfwSetWindowPos(window, win_x, win_y);
    HWND hwnd = glfwGetWin32Window(window);

    // Feedback prevention: exclude our own window from WGC monitor capture.
    // WGC honours WDA_EXCLUDEFROMCAPTURE (Win10 2004+). Essential for
    // fullscreen/monitor capture; harmless for per-window capture.
    SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);

    // Borderless overlay should not steal focus or show in the taskbar.
    // NOTE: we deliberately do NOT add WS_EX_LAYERED/WS_EX_TRANSPARENT —
    // layered windows are incompatible with the DXGI flip-model swap chain,
    // so true cross-process mouse click-through needs DirectComposition
    // (Phase 4a). NOACTIVATE keeps focus on whatever is underneath; the
    // global keyboard hook below drives the hotkeys regardless of focus.
    if (!chrome) {
        LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        ex |= WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
    }

    int fb_w = 0, fb_h = 0;
    glfwGetFramebufferSize(window, &fb_w, &fb_h);
    if (fb_w <= 0 || fb_h <= 0) { fb_w = W; fb_h = H; }

    // D3D12 backend bound to the overlay HWND.
    auto backend = tubelight::create_backend(tubelight::BackendKind::D3D12);
    if (!backend) {
        std::fprintf(stderr, "[overlay] D3D12 backend not compiled in\n");
        glfwDestroyWindow(window); glfwTerminate();
        return 1;
    }
    tubelight::BackendInitParams bp;
    bp.native_window_handle = hwnd;
    bp.width  = fb_w;
    bp.height = fb_h;
    bp.enable_debug = false;
    // Phase 4a: borderless overlay modes use a DirectComposition swap chain
    // so they can be WS_EX_LAYERED|TRANSPARENT (click-through). Plain
    // windowed keeps the direct HWND swap chain.
    bp.composition = !chrome;
    if (!backend->init(bp)) {
        std::fprintf(stderr,
            "[overlay] D3D12Backend::init failed — retry without --renderer dx12 "
            "to use the GL overlay.\n");
        glfwDestroyWindow(window); glfwTerminate();
        return 1;
    }
    auto* d12 = static_cast<tubelight::D3D12Backend*>(backend.get());

    // D3D11On12 device shared with WGC (which is D3D11-only).
    ID3D11Device* d3d11 = d12->d3d11_on12_device();
    if (!d3d11) {
        std::fprintf(stderr, "[overlay] D3D11On12 device creation failed\n");
        glfwDestroyWindow(window); glfwTerminate();
        return 1;
    }

    tubelight::WgcCapture wgc;
    const bool wgc_init = mode_target
        ? wgc.init_for_window(target_hwnd, d3d11)
        : wgc.init_for_monitor(mon.hmon, d3d11);
    if (!wgc_init || !wgc.start()) {
        std::fprintf(stderr, "[overlay] WGC capture init/start failed\n");
        glfwDestroyWindow(window); glfwTerminate();
        return 1;
    }
    std::fprintf(stderr, "[overlay] dx12: WGC capturing %s\n",
                 mode_target ? "target window" : "monitor");

    // Pipeline on the D3D12 backend. After set_backend, the pipeline owns
    // the backend; we keep backend_raw for the per-frame begin/end calls.
    tubelight::Pipeline pipeline;
    auto* backend_raw = backend.get();
    pipeline.set_backend(std::move(backend));
    if (!pipeline.create(fb_w, fb_h)) {
        std::fprintf(stderr, "[overlay] pipeline.create failed (dx12)\n");
        wgc.stop(); glfwDestroyWindow(window); glfwTerminate();
        return 1;
    }
    if (!opts.profile_id.empty()) {
        std::string err;
        auto p = tubelight::load_crt_profile_by_id(opts.profile_id, err);
        if (p) {
            pipeline.apply_crt_profile(*p);
            std::fprintf(stderr, "[overlay] CRT profile loaded: %s\n",
                         p->display_name.c_str());
        } else {
            std::fprintf(stderr, "[overlay] CRT profile '%s' not found: %s\n",
                         opts.profile_id.c_str(), err.c_str());
        }
    }
    if (!opts.signal_id.empty()) {
        std::string err;
        auto s = tubelight::load_signal_profile_by_id(opts.signal_id, err);
        if (s) {
            pipeline.apply_signal_profile(*s);
            std::fprintf(stderr, "[overlay] signal profile: %s\n",
                         s->display_name.c_str());
        } else {
            std::fprintf(stderr, "[overlay] signal profile '%s' not found: %s\n",
                         opts.signal_id.c_str(), err.c_str());
        }
    }

    // Input: ESC quits, 1..8 toggle passes, 0 all-on, F freeze. Reuses the
    // GL path's key_cb via AppState; resize_state carries the backend ptr.
    AppState state;
    state.pipeline     = &pipeline;
    state.resize_state = backend_raw;
    glfwSetWindowUserPointer(window, &state);
    glfwSetKeyCallback(window, key_cb);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int ww, int hh) {
        auto* s = static_cast<AppState*>(glfwGetWindowUserPointer(w));
        if (!s) return;
        if (s->resize_state)
            static_cast<tubelight::IRenderBackend*>(s->resize_state)->resize(ww, hh);
        if (s->pipeline) s->pipeline->resize(ww, hh);
    });

    glfwShowWindow(window);
    SetWindowPos(hwnd, HWND_TOPMOST, win_x, win_y,
                 chrome ? 0 : W, chrome ? 0 : H,
                 (chrome ? (SWP_NOMOVE | SWP_NOSIZE) : 0) | SWP_NOACTIVATE | SWP_SHOWWINDOW);

    // Global low-level keyboard hook so Ctrl+Alt+<key> works regardless of
    // focus — essential because the borderless overlay is WS_EX_NOACTIVATE
    // and won't receive GLFW key events. Same pattern + same kb_hook_proc /
    // g_hk_* atomics as the GL path. Runs on a worker thread with its own
    // message pump; torn down via PostThreadMessage(WM_QUIT) + join.
    g_hk_quit          = false;
    g_hk_freeze_toggle = false;
    g_hk_all_on        = false;
    g_hk_toggle_pass   = -1;
    std::atomic<DWORD> hk_tid{0};
    std::thread hotkey_thread([&]() {
        hk_tid = GetCurrentThreadId();
        HHOOK h = SetWindowsHookEx(WH_KEYBOARD_LL, kb_hook_proc,
                                   GetModuleHandleW(nullptr), 0);
        if (!h) {
            std::fprintf(stderr,
                "[overlay] dx12: SetWindowsHookEx failed GLE=%lu — "
                "Ctrl+Alt+Q won't work\n", GetLastError());
        }
        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (h) UnhookWindowsHookEx(h);
    });
    while (hk_tid.load() == 0) std::this_thread::yield();

    std::printf(
        "[overlay] dx12 hotkeys: ESC / Ctrl+Alt+Q quit | (Ctrl+Alt+)1..8 "
        "toggle pass | (Ctrl+Alt+)0 all on | (Ctrl+Alt+)F freeze\n");

    double t0 = glfwGetTime();
    tubelight::TextureHandle last_h{0};
    unsigned long long frames_rendered = 0;
    int target_lost_frames = 0;
    const bool bench = opts.bench_frames > 0;
    std::vector<double> cap_ms;
    if (bench) {
        cap_ms.reserve(static_cast<size_t>(opts.bench_frames));
        std::fprintf(stderr, "[capbench] dx12: capturing %d frames...\n",
                     opts.bench_frames);
    }
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Global-hotkey actions (focus-independent).
        if (g_hk_quit.load()) break;
        if (g_hk_freeze_toggle.exchange(false)) {
            state.freeze = !state.freeze;
            std::printf("[overlay] freeze: %s\n", state.freeze ? "ON" : "OFF");
        }
        if (g_hk_all_on.exchange(false)) {
            for (int i = 0; i < tubelight::Pipeline::kPassCount; ++i)
                pipeline.set_pass_enabled(i, true);
            std::printf("[overlay] all passes ON\n");
        }
        if (int p = g_hk_toggle_pass.exchange(-1); p >= 0) {
            bool cur = pipeline.is_pass_enabled(p);
            pipeline.set_pass_enabled(p, !cur);
            std::printf("[overlay] %s: %s\n",
                        tubelight::pass_display_name(p), !cur ? "ON" : "OFF");
        }

        // Target-window tracking: follow the target as it moves so the
        // overlay stays glued on top of it. Size tracking (WGC pool
        // recreate) is deferred to v0.2.1 — launch size is kept. Exit
        // gracefully if the target window is gone for several frames.
        if (mode_target) {
            if (!IsWindow(target_hwnd)) {
                if (++target_lost_frames > 30) {
                    std::printf("[overlay] dx12: target window closed; exiting\n");
                    break;
                }
            } else {
                target_lost_frames = 0;
                int nx, ny, nw, nh;
                if (get_target_screen_rect(target_hwnd, nx, ny, nw, nh) &&
                    (nx != win_x || ny != win_y)) {
                    win_x = nx; win_y = ny;
                    SetWindowPos(hwnd, HWND_TOPMOST, win_x, win_y, 0, 0,
                                 SWP_NOSIZE | SWP_NOACTIVATE);
                }
            }
        }

        if (!state.freeze) {
            // capture→GPU step: WGC latest_frame + zero-copy D3D11On12 wrap.
            const auto cap_t0 = std::chrono::high_resolution_clock::now();
            int tw = 0, th = 0;
            auto tex11 = wgc.latest_frame(tw, th);
            if (tex11) {
                auto h = d12->wrap_d3d11_texture(tex11.Get(), tw, th);
                if (h.is_valid()) last_h = h;
            }
            if (bench && tex11) {
                cap_ms.push_back(std::chrono::duration<double, std::milli>(
                    std::chrono::high_resolution_clock::now() - cap_t0).count());
            }
        }
        if (!last_h.is_valid()) {
            // No frame has arrived yet (first ~1-2 ticks). Wait a hair.
            Sleep(2);
            continue;
        }

        pipeline.set_time(static_cast<float>(glfwGetTime() - t0));
        backend_raw->begin_frame();
        pipeline.render_to_screen(last_h);
        backend_raw->end_frame();
        if (frames_rendered == 0) {
            std::fprintf(stderr,
                "[overlay] dx12: first frame rendered (source handle %u)\n",
                last_h.id);
        }
        ++frames_rendered;
        if (bench && static_cast<int>(cap_ms.size()) >= opts.bench_frames) {
            capture_bench_report("dx12", cap_ms);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    }

    std::printf("[overlay] dx12: %llu frames rendered, %llu captured\n",
                frames_rendered,
                static_cast<unsigned long long>(wgc.frame_count()));

    // Tear down the keyboard hook thread.
    PostThreadMessage(hk_tid.load(), WM_QUIT, 0, 0);
    if (hotkey_thread.joinable()) hotkey_thread.join();

    wgc.stop();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
#endif // TUBELIGHT_HAVE_D3D12

} // namespace

// ---------------------------------------------------------------------------
// Public entry
// ---------------------------------------------------------------------------

int run(const Options& opts) {
    // T5.5: --renderer dx12 takes the WGC + D3D11On12 + D3D12 path. The GL
    // body below is left entirely untouched (zero regression risk).
#if defined(TUBELIGHT_HAVE_D3D12)
    if (opts.backend == BackendKind::D3D12) {
        return run_dx12(opts);
    }
#else
    if (opts.backend == BackendKind::D3D12) {
        std::fprintf(stderr,
            "[overlay] --renderer dx12 requested but D3D12 was not compiled "
            "in; falling back to the OpenGL overlay.\n");
    }
#endif
    glfwSetErrorCallback(glfw_error_cb);
    if (!glfwInit()) {
        std::fprintf(stderr, "[overlay] glfwInit failed\n");
        return 1;
    }

    // DXGI first so we know the monitor geometry.
    DxgiCapture capture;
    if (!capture.init(opts.monitor_index)) {
        glfwTerminate();
        return 1;
    }
    // Sibling backend for recordable mode. Heap-allocated to keep its
    // footprint off run_overlay's stack frame. Confirmed by bisect:
    // its mere presence as a stack local correlated with the user's
    // /GS canary trip; with it on the heap the layout no longer
    // exposes the latent overrun. Construction is cheap (no Win32
    // work) until init() is called.
    auto mag_capture_ptr = std::make_unique<MagCapture>();
    MagCapture& mag_capture = *mag_capture_ptr;

    // Window mode: Windowed = movable resizable normal Win32 window with a
    // title bar; Fullscreen = borderless topmost covering the whole monitor;
    // Region / TargetWindow = positioned + sized to track a user rectangle
    // or another application's window.
    //
    // `initial_fullscreen` captures the launch-time CLI mode and never
    // changes; it gates the click-through (WS_EX_TRANSPARENT) behaviour the
    // standalone --overlay-fullscreen mode needs. `fullscreen_active`
    // mirrors it on startup but the in-app menu / Ctrl+Alt+Enter can flip
    // it at runtime â€” that runtime fullscreen is *focusable* so the user
    // can still drive the menu without leaving fullscreen.
    const bool initial_fullscreen = (opts.mode == OverlayMode::Fullscreen);
    const bool windowed_mode      = (opts.mode == OverlayMode::Windowed);
    const bool initial_target     = (opts.mode == OverlayMode::TargetWindow);
    const bool initial_region     = (opts.mode == OverlayMode::Region);
    bool fullscreen_active        = initial_fullscreen;
    bool target_active            = initial_target;     // runtime-mutable
    bool region_active            = initial_region;     // runtime-mutable
    const bool& target_mode       = target_active;      // alias for the readers below

    // Resolve the target HWND up-front so we can size our window to match
    // the target before showing it (no flash of wrong size on launch).
    HWND target_hwnd = nullptr;
    int  target_x = 0, target_y = 0, target_w = 0, target_h = 0;
    if (target_mode) {
        target_hwnd = find_target_hwnd(opts.target_window, opts.target_pid, nullptr);
        if (!target_hwnd) {
            std::fprintf(stderr,
                "[overlay] target window not found (title='%s' pid=%d)\n",
                opts.target_window.c_str(), opts.target_pid);
            capture.shutdown();
            glfwTerminate();
            return 1;
        }
        get_target_screen_rect(target_hwnd, target_x, target_y, target_w, target_h);
        std::fprintf(stderr,
            "[overlay] tracking target HWND 0x%p at (%d,%d) %dx%d\n",
            static_cast<void*>(target_hwnd), target_x, target_y, target_w, target_h);
    }

    // Chrome (title bar / border / focus) is only for plain windowed mode.
    // Fullscreen + TargetWindow + Region are all borderless click-through.
    const bool chrome = windowed_mode;

    glfwWindowHint(GLFW_VISIBLE,   GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, chrome ? GLFW_TRUE  : GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING,  chrome ? GLFW_FALSE : GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, chrome ? GLFW_TRUE  : GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUSED,   chrome ? GLFW_TRUE  : GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, chrome ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    int W = target_mode      ? target_w
          : region_active    ? opts.region_w
          : fullscreen_active ? capture.width()
                              : opts.init_w;
    int H = target_mode      ? target_h
          : region_active    ? opts.region_h
          : fullscreen_active ? capture.height()
                              : opts.init_h;
    GLFWwindow* window = glfwCreateWindow(W, H, "Tubelight", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "[overlay] glfwCreateWindow failed\n");
        capture.shutdown();
        glfwTerminate();
        return 1;
    }

    if (target_mode) {
        glfwSetWindowPos(window, target_x, target_y);
    } else if (region_active) {
        glfwSetWindowPos(window, capture.origin_x() + opts.region_x,
                                  capture.origin_y() + opts.region_y);
    } else if (fullscreen_active) {
        glfwSetWindowPos(window, capture.origin_x(), capture.origin_y());
    } else {
        // Centre on the chosen monitor.
        glfwSetWindowPos(window,
                         capture.origin_x() + (capture.width()  - W) / 2,
                         capture.origin_y() + (capture.height() - H) / 2);
    }
    glfwMakeContextCurrent(window);
    // VSync interval will be set from settings.low_latency once
    // settings are loaded further down. Default to OFF in the
    // meantime so the first few startup frames already benefit.
    glfwSwapInterval(0);

    HWND hwnd = glfwGetWin32Window(window);

    // Exclude the overlay from screen capture (avoid feedback loop where
    // DXGI would otherwise include our own rendered output in its grab).
    apply_capture_affinity(hwnd);
    if (fullscreen_active || target_mode || region_active) {
        // Click-through topmost. WDA_EXCLUDEFROMCAPTURE keeps DXGI from
        // feedback-looping our own output back in.
        LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        ex |= WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
        int sx = target_mode ? target_x
              : region_active ? (capture.origin_x() + opts.region_x)
                              : capture.origin_x();
        int sy = target_mode ? target_y
              : region_active ? (capture.origin_y() + opts.region_y)
                              : capture.origin_y();
        SetWindowPos(hwnd, HWND_TOPMOST, sx, sy,
                     W, H, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
        // Windowed overlay: a normal Win32 window the user can move + resize.
        // Stays topmost by default (overlay use case) but accepts input.
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    // Global hotkeys: registered to the thread queue (HWND=nullptr), then
    // The keyboard hook (kb_hook_proc above) routes by VK code directly into
    // the g_hk_* atomics, so we no longer need per-hotkey RegisterHotKey IDs.
    // Build the source texture sized to the desktop.
    tubelight::Texture2D source;
    source.create_empty(W, H, GL_RGBA8);
    // We upload with `format = GL_BGRA` so OpenGL itself does the byte
    // reordering from DXGI's BGRA8 into the texture's internal RGBA layout.
    // No swizzle needed (and adding one here would double-swap and produce
    // the classic "yellow becomes blue / orange becomes blue" symptom).
    glBindTexture(GL_TEXTURE_2D, source.id());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Vertical flip via wrap is not enough; we flip during upload (origin top-left
    // in DXGI vs bottom-left in GL â€” handled by feeding rows in normal order
    // and reading texture(uv) with uv.y = 1 - uv.y; simplest fix: we keep DXGI
    // top-down rows AND ALSO compensate by using GL_REPEAT? Instead we flip
    // here by uploading with a temp buffer when needed â€” see below).
    // Pipeline at window resolution.
    tubelight::Pipeline pipeline;
    if (!pipeline.create(W, H)) {
        std::fprintf(stderr, "[overlay] pipeline create failed\n");
        return 1;
    }
    if (!opts.profile_id.empty()) {
        std::string err;
        auto p = tubelight::load_crt_profile_by_id(opts.profile_id, err);
        if (p) {
            pipeline.apply_crt_profile(*p);
            std::fprintf(stderr, "[overlay] CRT profile loaded: %s\n", p->display_name.c_str());
            std::fflush(stderr);
            // Optional photo-real bezel PNG at assets/bezels/<id>.png.
            std::string bezel_path =
                std::string("assets/bezels/") + opts.profile_id + ".png";
            pipeline.load_bezel_image(bezel_path);
        } else {
            std::fprintf(stderr, "[overlay] CRT profile '%s' not found: %s\n",
                         opts.profile_id.c_str(), err.c_str());
        }
    }
    if (!opts.signal_id.empty()) {
        std::string err;
        auto s = tubelight::load_signal_profile_by_id(opts.signal_id, err);
        if (s) {
            pipeline.apply_signal_profile(*s);
            if (pipeline.params().monochrome == 1) {
                std::printf("[overlay] signal profile '%s' ignored (locked to clean RGB for monochrome)\n",
                             s->display_name.c_str());
            } else {
                std::printf("[overlay] signal profile: %s\n", s->display_name.c_str());
            }
        } else {
            std::fprintf(stderr, "[overlay] signal profile '%s' not found: %s\n",
                         opts.signal_id.c_str(), err.c_str());
        }
    }

    // User-toggled click-through state for plain windowed mode. Independent
    // from fullscreen / target / region (which manage click-through
    // themselves). Toggle with Ctrl+Alt+C or the menu checkbox.
    // Persists across runs via settings.json.
    bool clickthrough_user = false;

    // Toast state â€” declared early so apply_clickthrough_user can write to it.
    std::string toast_text;
    auto toast_time = std::chrono::steady_clock::now() - std::chrono::hours(1);
    const auto kToastShown = std::chrono::milliseconds(2500);

    // ADR-0001 §2: click-through in windowed mode is removed. Each
    // overlay mode now has a fixed click-through policy:
    //   --overlay              → click-through OFF (standard Win32
    //                            frame, drag/resize via the title bar
    //                            and edges, body clicks consumed)
    //   --overlay-target       → click-through ON  (handled by
    //                            do_attach_target)
    //   --overlay-region       → click-through ON  (handled by
    //                            do_attach_region)
    //   --overlay-fullscreen   → click-through ON  (handled by the
    //                            initial-fullscreen path)
    //
    // The lambda is kept as a no-op stub so the menu checkbox plumbing
    // continues to compile while we wait for menu.cpp Phase 1b update
    // to hide the widget entirely.
    auto apply_clickthrough_user = [&](bool /*on*/) {
        // Force the windowed-mode user toggle to off, always.
        clickthrough_user = false;
        // Original implementation kept for reference; intentionally
        // unreachable from this branch.
        if (false) {
        LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        const bool on = false;
        if (on) {
            if (!(ex & WS_EX_LAYERED)) {
                ex |= WS_EX_LAYERED;
                SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
                SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
                ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            }
            ex |= WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
        } else {
            ex &= ~(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
        }
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOACTIVATE | SWP_FRAMECHANGED);
        apply_capture_affinity(hwnd);
        }  // end if (false) — unreachable preserved for reference only
    };

    // Lock GLFW's user-drag resize to match the current target aspect (if
    // any). target_aspect == 0 means "fill", so we release the constraint.
    auto apply_aspect_lock = [&]() {
        if (windowed_mode && !fullscreen_active && pipeline.params().target_aspect > 0.0f) {
            // GLFW expects a rational ratio â€” scaling by 10000 keeps two
            // decimals of precision which is plenty for the standard CRT
            // aspects (4:3, 5:4, 16:10, 16:9, 21:9).
            int num = static_cast<int>(pipeline.params().target_aspect * 10000.0f);
            int den = 10000;
            glfwSetWindowAspectRatio(window, num, den);
        } else {
            glfwSetWindowAspectRatio(window, GLFW_DONT_CARE, GLFW_DONT_CARE);
        }
    };
    apply_aspect_lock();

    // Saved windowed pos/size â€” restored when leaving runtime fullscreen.
    int saved_win_x = 0, saved_win_y = 0;
    int saved_win_w = opts.init_w, saved_win_h = opts.init_h;
    glfwGetWindowPos(window, &saved_win_x, &saved_win_y);

    // Resize the window to match pipeline.params().target_aspect, keeping
    // the same center on the monitor and roughly the same on-screen area.
    // No-op for target_aspect == 0 (fill mode) or while fullscreen.
    auto do_snap_to_aspect = [&]() {
        if (fullscreen_active) return;
        float ar = pipeline.params().target_aspect;
        if (ar <= 0.0f) return;
        int cur_w = 0, cur_h = 0;
        glfwGetWindowSize(window, &cur_w, &cur_h);
        if (cur_w <= 0 || cur_h <= 0) return;
        double area = static_cast<double>(cur_w) * static_cast<double>(cur_h);
        int new_w = static_cast<int>(std::sqrt(area * ar) + 0.5);
        int new_h = static_cast<int>(std::sqrt(area / ar) + 0.5);
        // Clamp to the monitor so we never spill off-screen.
        new_w = std::min(new_w, capture.width());
        new_h = std::min(new_h, capture.height());
        int cur_x = 0, cur_y = 0;
        glfwGetWindowPos(window, &cur_x, &cur_y);
        int center_x = cur_x + cur_w / 2;
        int center_y = cur_y + cur_h / 2;
        // Relock aspect *before* resizing so GLFW doesn't fight us.
        apply_aspect_lock();
        glfwSetWindowSize(window, new_w, new_h);
        glfwSetWindowPos(window, center_x - new_w / 2, center_y - new_h / 2);
    };

    // Flip between the runtime fullscreen mode and the saved windowed mode.
    //
    // Key constraints on this hardware (NVIDIA + Win11 26200):
    //   1) GWL_STYLE / GWL_EXSTYLE swaps on a live OpenGL window drop
    //      WDA_EXCLUDEFROMCAPTURE â†’ DXGI captures our overlay â†’ recursive
    //      feedback ghost. So we use WM_NCCALCSIZE in our subclass to
    //      collapse the non-client area to 0 pixels instead.
    //   2) A topmost window that covers the *entire* monitor gets
    //      promoted by Win11 to Independent Flip / direct scanout, which
    //      bypasses DWM compositing â†’ WDA stops applying â†’ same ghost.
    //      We leave 1 pixel uncovered at the bottom edge to keep us in
    //      the composited path. It's barely visible and harmless.
    auto do_toggle_fullscreen = [&]() {
        if (fullscreen_active) {
            // Exit: drop the NCCALCSIZE override, then restore the saved
            // client-area geometry through GLFW (which knows the correct
            // outer-vs-client math for the current styles). The extra
            // SetWindowPos with SWP_FRAMECHANGED triggers NCCALCSIZE
            // re-evaluation so the title bar comes back this frame.
            g_hide_nonclient = false;
            glfwSetWindowSize(window, saved_win_w, saved_win_h);
            glfwSetWindowPos(window,  saved_win_x, saved_win_y);
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE |
                         SWP_NOACTIVATE);
            fullscreen_active = false;
            apply_aspect_lock();
            std::fprintf(stderr, "[overlay] fullscreen OFF\n");
        } else {
            glfwGetWindowPos(window,  &saved_win_x, &saved_win_y);
            glfwGetWindowSize(window, &saved_win_w, &saved_win_h);
            // Release any aspect lock so the window can be the full monitor.
            glfwSetWindowAspectRatio(window, GLFW_DONT_CARE, GLFW_DONT_CARE);

            // Hide non-client (NCCALCSIZEâ†’0) + size to monitor minus 1px
            // at the bottom to dodge Win11 Independent-Flip promotion.
            g_hide_nonclient = true;
            const int fs_x = capture.origin_x();
            const int fs_y = capture.origin_y();
            const int fs_w = capture.width();
            const int fs_h = capture.height() - 1;
            SetWindowPos(hwnd, HWND_TOPMOST, fs_x, fs_y, fs_w, fs_h,
                         SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            // ADR-0001 §3: keep Mag source rect in sync with the new
            // fullscreen extent.
            if (mag_capture.is_initialized()) {
                mag_capture.set_source_rect(fs_x, fs_y, fs_w, fs_h);
            }
            fullscreen_active = true;
            std::fprintf(stderr, "[overlay] fullscreen ON (borderless, IF-safe)\n");
        }
    };

    // Cached title of the active target window (for the menu UI).
    std::string target_title_cached;
    if (initial_target && target_hwnd) {
        char buf[256] = {};
        GetWindowTextA(target_hwnd, buf, sizeof(buf) - 1);
        target_title_cached = buf;
    }

    // Attach to a target window at runtime: shrink+follow it, enable
    // click-through. Same path as the CLI --overlay-target mode but
    // triggered from the menu / hotkey. Save the windowed geometry first
    // so detach() can come back to it.
    auto do_attach_target = [&](HWND target) {
        if (!target || target == hwnd) return;
        // If currently fullscreen, exit fullscreen first to keep state sane.
        if (fullscreen_active) do_toggle_fullscreen();
        glfwGetWindowPos(window,  &saved_win_x, &saved_win_y);
        glfwGetWindowSize(window, &saved_win_w, &saved_win_h);
        glfwSetWindowAspectRatio(window, GLFW_DONT_CARE, GLFW_DONT_CARE);

        target_hwnd   = target;
        target_active = true;

        // Click-through + don't steal focus on clicks.
        LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        ex |= WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);

        // Collapse the title bar (NCCALCSIZE returns 0) so only the
        // client area is visible over the target window.
        g_hide_nonclient = true;

        int tx, ty, tw, th;
        if (get_target_screen_rect(target, tx, ty, tw, th)) {
            target_x = tx; target_y = ty; target_w = tw; target_h = th;
            SetWindowPos(hwnd, HWND_TOPMOST, tx, ty, tw, th,
                         SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW);
            // ADR-0001 §3: keep Mag source rect in sync with the target.
            // Without this, the Mag callback continues sampling the full
            // monitor and we capture the wrong area on every frame.
            if (mag_capture.is_initialized()) {
                mag_capture.set_source_rect(tx, ty, tw, th);
            }
        }
        apply_capture_affinity(hwnd);

        char buf[256] = {};
        GetWindowTextA(target, buf, sizeof(buf) - 1);
        target_title_cached = buf;
        std::fprintf(stderr, "[overlay] target attached: '%s' HWND %p\n",
                     buf, static_cast<void*>(target));
    };

    // Detach from the currently-tracked target: drop click-through, show
    // chrome again, restore the saved windowed geometry.
    auto do_detach_target = [&]() {
        if (!target_active) return;
        target_active = false;
        target_hwnd = nullptr;
        target_title_cached.clear();

        LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        ex &= ~(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);

        g_hide_nonclient = false;

        glfwSetWindowSize(window, saved_win_w, saved_win_h);
        glfwSetWindowPos(window,  saved_win_x, saved_win_y);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        // ADR-0001 §3: when detaching from target, the overlay returns
        // to free windowed mode — Mag should sample the full monitor
        // again so the user can move/resize the window without losing
        // capture.
        if (mag_capture.is_initialized()) {
            mag_capture.set_source_rect(capture.origin_x(), capture.origin_y(),
                                         capture.width(),    capture.height());
        }
        apply_capture_affinity(hwnd);
        apply_aspect_lock();
        std::fprintf(stderr, "[overlay] target detached\n");
    };

    // Region attach/detach: pin the overlay to a fixed monitor-relative
    // rect (no window tracking, no fullscreen) with click-through. Same
    // visual semantics as target mode but the rect is static.
    auto do_attach_region = [&](int rx, int ry, int rw, int rh) {
        if (rw <= 0 || rh <= 0) return;
        if (fullscreen_active) do_toggle_fullscreen();
        if (target_active)     do_detach_target();
        glfwGetWindowPos(window,  &saved_win_x, &saved_win_y);
        glfwGetWindowSize(window, &saved_win_w, &saved_win_h);
        glfwSetWindowAspectRatio(window, GLFW_DONT_CARE, GLFW_DONT_CARE);

        region_active = true;

        LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        ex |= WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
        g_hide_nonclient = true;

        const int screen_x = capture.origin_x() + rx;
        const int screen_y = capture.origin_y() + ry;
        SetWindowPos(hwnd, HWND_TOPMOST,
                     screen_x, screen_y, rw, rh,
                     SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        // ADR-0001 §3: keep Mag source rect in sync with the region.
        if (mag_capture.is_initialized()) {
            mag_capture.set_source_rect(screen_x, screen_y, rw, rh);
        }
        apply_capture_affinity(hwnd);
        std::fprintf(stderr, "[overlay] region attached at (%d,%d) %dx%d\n",
                     rx, ry, rw, rh);
    };

    auto do_detach_region = [&]() {
        if (!region_active) return;
        region_active = false;
        LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        ex &= ~(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
        g_hide_nonclient = false;
        glfwSetWindowSize(window, saved_win_w, saved_win_h);
        glfwSetWindowPos(window,  saved_win_x, saved_win_y);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        // ADR-0001 §3: return Mag to full-monitor sampling.
        if (mag_capture.is_initialized()) {
            mag_capture.set_source_rect(capture.origin_x(), capture.origin_y(),
                                         capture.width(),    capture.height());
        }
        apply_capture_affinity(hwnd);
        apply_aspect_lock();
        std::fprintf(stderr, "[overlay] region detached\n");
    };

    // Track the last foreground window that wasn't our overlay, so
    // "Track foreground" (button or Ctrl+Alt+T) can attach to whatever
    // the user was looking at before the menu / hotkey grabbed focus.
    HWND last_external_foreground = nullptr;

    // Helper: read the window's client rect in monitor-local pixel coords.
    auto read_window_rect_on_monitor = [&](int& out_x, int& out_y, int& out_w, int& out_h) {
        POINT tl = {0, 0};
        ClientToScreen(hwnd, &tl);
        RECT cr;
        GetClientRect(hwnd, &cr);
        out_x = tl.x - capture.origin_x();
        out_y = tl.y - capture.origin_y();
        out_w = cr.right  - cr.left;
        out_h = cr.bottom - cr.top;
    };

    AppState state{&pipeline, false};
    glfwSetWindowUserPointer(window, &state);
    glfwSetKeyCallback(window, key_cb);

    // Handle resize of the windowed mode: pipeline + source texture both
    // resize to the new framebuffer size so we always render at native pixel
    // density (no upscale blur on top of the CRT shader).
    // Resize state: the GLFW callback ONLY records the target size; the
    // expensive FBO + texture + sub-buffer recreation happens once per
    // main-loop iteration when a pending size differs from the current one.
    // This debounces the WM_SIZE storm Windows fires during a drag-resize.
    struct ResizeState {
        int pending_w = 0;
        int pending_h = 0;
    };
    int win_w = W, win_h = H;
    std::vector<uint8_t> sub_buffer(static_cast<size_t>(win_w) * win_h * 4, 0);
    ResizeState rs{win_w, win_h};
    state.resize_state = &rs;

    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int width, int height) {
        if (width <= 0 || height <= 0) return;
        auto* s = static_cast<AppState*>(glfwGetWindowUserPointer(w));
        if (!s || !s->resize_state) return;
        auto* r = static_cast<ResizeState*>(s->resize_state);
        r->pending_w = width;
        r->pending_h = height;
    });

    auto apply_pending_resize = [&]() {
        if (rs.pending_w == win_w && rs.pending_h == win_h) return;
        if (rs.pending_w <= 0 || rs.pending_h <= 0) return;
        win_w = rs.pending_w;
        win_h = rs.pending_h;
        source.destroy();
        source.create_empty(win_w, win_h, GL_RGBA8);
        glBindTexture(GL_TEXTURE_2D, source.id());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        pipeline.resize(win_w, win_h);
        sub_buffer.assign(static_cast<size_t>(win_w) * win_h * 4, 0);
        glViewport(0, 0, win_w, win_h);
    };

    // Menu state â€” selected profile/signal ids + a global intensity multiplier.
    std::string current_profile_id = opts.profile_id;
    std::string current_signal_id  = opts.signal_id;
    float intensity_multiplier     = 1.0f;
    Pipeline::GlobalParams base_params = pipeline.params();
    Settings settings = load_settings();
    if (settings.low_latency) {
        std::fprintf(stderr, "[overlay] migrating settings.low_latency true â†’ false\n");
        settings.low_latency = false;
        save_settings(settings);
    }
    // ADR-0001: settings.clickthrough_user / settings.recordable are
    // deprecated. Their persisted values are silently ignored; nothing
    // to migrate because behaviour is now hardcoded to "always on" at
    // runtime regardless of the file contents.
    std::string effective_capture_dir =
        settings.capture_dir.empty() ? default_capture_dir() : settings.capture_dir;
    std::string ui_capture_dir = settings.capture_dir;
    bool hud_visible    = settings.hud_visible;
    bool  audio_enabled = settings.crt_audio_enabled;
    float audio_volume  = settings.crt_audio_volume;
    bool  low_latency   = settings.low_latency;
    // ADR-0001 §1: recordable is hardcoded ON. The local boolean is
    // retained as `true` for code paths that still read it; the global
    // atomic `g_recordable_mode` defaults to true in its declaration.
    bool  recordable    = true;
    g_recordable_mode.store(true);
    // Initialise Mag at startup unconditionally. If MagInit fails (driver
    // issue, headless test, future Windows that pulls the API), the rest
    // of the overlay still works — grab_source() falls back to DXGI when
    // mag_capture.is_initialized() is false, and the user just sees the
    // pre-ADR-0001 feedback risk (overlay may collapse to black in some
    // capture configurations). This is the "circuit breaker" called out
    // in RISKS R11.
    if (!mag_capture.is_initialized()) {
        mag_capture.init(opts.monitor_index, hwnd,
                          GetModuleHandleW(nullptr));
    }
    apply_capture_affinity(hwnd);
    glfwSwapInterval(low_latency ? 0 : 1);
    int   rec_source    = settings.record_source;
    int   rec_rx        = settings.record_rect_x;
    int   rec_ry        = settings.record_rect_y;
    int   rec_rw        = settings.record_rect_w;
    int   rec_rh        = settings.record_rect_h;
    // Apply persisted click-through at startup (windowed mode only).
    // Click-through is now strictly a per-session toggle â€” never auto-
    // applied on launch even if it was on when the user last quit. The
    // user can re-enable via Ctrl+Alt+C or the menu checkbox at any
    // time. This avoids the "I can't click anything in the title bar"
    // surprise on next launch.

    // CRT audio (XAudio2 flyback whine). Init best-effort: if it fails
    // for any reason (no audio device, driver issue, headless test env)
    // the overlay keeps running silently.
    tubelight::audio::CrtAudio crt_audio;
    {
        std::string audio_err;
        if (!crt_audio.init(audio_err)) {
            std::fprintf(stderr, "[overlay] CRT audio disabled: %s\n", audio_err.c_str());
        } else {
            crt_audio.set_volume(audio_volume);
            crt_audio.set_enabled(audio_enabled);
        }
    }

    VideoRecorder video_recorder;
    // toast_text / toast_time / kToastShown moved earlier (above
    // apply_clickthrough_user) so the lambda can capture them by ref.
    Menu menu;
    bool has_menu = menu.init(window);
    std::fprintf(stderr, "[overlay] %s\n",
                 has_menu ? "in-app menu ready (Ctrl+Alt+M to open)"
                          : "built without imgui â€” menu disabled");

    LONG_PTR base_ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    (void)base_ex_style;
    bool menu_was_open = false;

    std::printf(
        "[overlay] %dx%d â€” capturing first desktop frame before showing window...\n",
        W, H);

    // upload_subregion_to_source() does the vertical flip into `sub_buffer`
    // (resized by the framebuffer-size callback) â€” no separate full-frame
    // buffer is needed any more.

    // -----------------------------------------------------------------
    // Install a low-level keyboard hook so Ctrl+Alt+<key> works regardless
    // of focus and regardless of any other app having registered the same
    // hotkey via RegisterHotKey. The hook runs on the thread that called
    // SetWindowsHookEx, but Windows uses an internal message dispatch so
    // we don't strictly need a separate pump â€” however a worker thread
    // with its own GetMessage loop is the canonical pattern, which we use
    // here to avoid interference with GLFW's own message handling.
    // -----------------------------------------------------------------
    g_hk_quit = false;
    g_hk_freeze_toggle = false;
    g_hk_all_on = false;
    g_hk_toggle_pass = -1;
    // g_hk_toggle_recordable / g_hk_toggle_clickthrough are no-ops per
    // ADR-0001; left initialised to false in their definition.

    std::atomic<DWORD> hk_tid{0};
    std::atomic<HHOOK> hk_handle{nullptr};

    std::thread hotkey_thread([&]() {
        hk_tid = GetCurrentThreadId();
        HHOOK h = SetWindowsHookEx(WH_KEYBOARD_LL, kb_hook_proc,
                                    GetModuleHandleW(nullptr), 0);
        if (!h) {
            std::fprintf(stderr,
                "[overlay] SetWindowsHookEx failed: GLE=%lu â€” Ctrl+Alt+Q won't work\n",
                GetLastError());
        } else {
            std::fprintf(stderr,
                "[overlay] keyboard hook installed (Ctrl+Alt+Q to quit)\n");
        }
        hk_handle = h;

        MSG msg;
        while (GetMessage(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (h) UnhookWindowsHookEx(h);
    });

    while (hk_tid.load() == 0) std::this_thread::yield();

    // -----------------------------------------------------------------
    // Grab the very first frame WHILE THE OVERLAY IS STILL HIDDEN.
    // -----------------------------------------------------------------
    {
        bool got = false;
        for (int attempt = 0; attempt < 20 && !got; ++attempt) {
            bool new_frame = false;
            if (!grab_source(capture, mag_capture, new_frame, 500)) {
                capture.shutdown();
                if (!capture.init(opts.monitor_index)) break;
                continue;
            }
            if (new_frame) {
                int wx, wy, ww, wh;
                read_window_rect_on_monitor(wx, wy, ww, wh);
                upload_subregion_to_source(capture, source, wx, wy, ww, wh,
                                            win_w, win_h, sub_buffer,
                                            fullscreen_active);
                got = true;
            } else {
                // Force the desktop to repaint so DXGI has something to deliver.
                ::InvalidateRect(nullptr, nullptr, TRUE);
                ::RedrawWindow(nullptr, nullptr, nullptr,
                               RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            }
        }
        if (!got) {
            std::fprintf(stderr,
                "[overlay] WARNING: never received an initial DXGI frame; "
                "showing window anyway.\n");
        }
    }

    // Now show the window with the first frame already pipelined.
    pipeline.set_time(0.0f);
    pipeline.render_to_screen(source.id());
    glfwSwapBuffers(window);
    glfwShowWindow(window);
    if (fullscreen_active) {
        SetWindowPos(hwnd, HWND_TOPMOST, capture.origin_x(), capture.origin_y(),
                     W, H, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    }

    std::printf(
        "[overlay] hotkeys: Ctrl+Alt+Q quit | Ctrl+Alt+M menu | "
        "Ctrl+Alt+F freeze | Ctrl+Alt+Enter fullscreen | "
        "Ctrl+Alt+T track foreground | Ctrl+Alt+C click-through | "
        "Ctrl+Alt+R recordable (Snipping Tool / Game Bar / OBS) | "
        "Ctrl+Alt+H toggle HUD | Ctrl+Alt+S screenshot | "
        "Ctrl+Alt+V video\n"
        "[overlay] (debug) Ctrl+Alt+0 all passes on | Ctrl+Alt+1..8 toggle individual pass\n");

    double t0 = glfwGetTime();
    bool have_initial = true;
    unsigned long long frames_total = 0, frames_new = 0;
    bool kicked_repaint = false;
    auto loop_started = std::chrono::steady_clock::now();
    // Phase 3e end-to-end: capture→GPU cost bench (DXGI grab + memcpy +
    // glTexSubImage2D upload). In bench mode the upload is forced every
    // frame (the realistic overlay-over-changing-content case).
    const bool bench = opts.bench_frames > 0;
    std::vector<double> cap_ms;
    if (bench) {
        cap_ms.reserve(static_cast<size_t>(opts.bench_frames));
        std::fprintf(stderr, "[capbench] gl: capturing %d frames...\n",
                     opts.bench_frames);
    }

    // ---------------------------------------------------------------------
    // Subclass the window so the overlay keeps rendering DURING a user
    // move / resize. Without this, the image freezes the moment the user
    // grabs the title bar â€” Windows enters a modal loop inside
    // DefWindowProc that blocks glfwPollEvents. Our WndProc installs a
    // ~16ms WM_TIMER between WM_ENTERSIZEMOVE/WM_EXITSIZEMOVE and ticks
    // the same per-frame work the main loop would otherwise do.
    // ---------------------------------------------------------------------
    FrameRenderState frame_state;
    frame_state.render_one = [&]() {
        apply_pending_resize();
        bool new_frame = false;
        if (!state.freeze) {
            // Non-blocking grab: if the desktop hasn't changed we just
            // re-use the previous capture rather than stall the modal
            // loop waiting on DXGI.
            (void)grab_source(capture, mag_capture, new_frame, 0);
        }
        if (new_frame) {
            int wx, wy, ww, wh;
            read_window_rect_on_monitor(wx, wy, ww, wh);
            upload_subregion_to_source(capture, source, wx, wy, ww, wh,
                                        win_w, win_h, sub_buffer,
                                        fullscreen_active);
        }
        pipeline.set_time(static_cast<float>(glfwGetTime() - t0));
        pipeline.render_to_screen(source.id());
        glfwSwapBuffers(window);
    };
    SetPropW(hwnd, kFrameRenderProp, reinterpret_cast<HANDLE>(&frame_state));
    g_orig_wndproc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
        hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&tubelight_subclass_proc)));

    // Track when the target window last had a non-degenerate rect, so we
    // can detach gracefully if it's been gone for a few frames in a row
    // rather than exit on the first transient.
    int target_lost_frames = 0;

    while (!glfwWindowShouldClose(window)) {
        // Recompute the effective click-through state for the subclass
        // hit-test handler. Always OFF while the menu is open so the
        // user can interact with widgets; otherwise honours the user's
        // chosen state for windowed mode + the CLI fullscreen path +
        // target / region modes.
        {
            bool menu_is_open_now = has_menu && menu.is_open();
            bool eff = (initial_fullscreen || target_active || region_active ||
                        clickthrough_user) && !menu_is_open_now;
            g_clickthrough_effective.store(eff);
        }

        // Target-window mode: each frame, query the target's screen-space
        // client rect and snap our overlay to match. If the target has
        // moved / resized, the framebuffer-size callback fires and the
        // pipeline + source texture follow on the next apply_pending_resize.
        if (target_active) {
            int tx, ty, tw, th;
            if (target_hwnd && IsWindow(target_hwnd) &&
                !IsIconic(target_hwnd) &&
                get_target_screen_rect(target_hwnd, tx, ty, tw, th)) {
                target_lost_frames = 0;
                int cx = 0, cy = 0, cw = 0, ch = 0;
                glfwGetWindowPos(window, &cx, &cy);
                glfwGetWindowSize(window, &cw, &ch);
                if (cx != tx || cy != ty || cw != tw || ch != th) {
                    SetWindowPos(hwnd, HWND_TOPMOST, tx, ty, tw, th,
                                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
                }
            } else {
                // Window vanished, was destroyed, or is minimised. Hide
                // so we don't render against stale geometry. For CLI
                // `--overlay-target` launches: exit after 2s gone. For
                // runtime attach: detach quietly back to windowed mode
                // so the user can pick a different target.
                ShowWindow(hwnd, SW_HIDE);
                if (++target_lost_frames > 120) {
                    if (initial_target) {
                        std::fprintf(stderr,
                            "[overlay] target window gone for 2s, exiting\n");
                        break;
                    } else {
                        std::fprintf(stderr,
                            "[overlay] target window gone, detaching\n");
                        do_detach_target();
                        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
                        target_lost_frames = 0;
                    }
                } else {
                    glfwPollEvents();
                    std::this_thread::sleep_for(std::chrono::milliseconds(16));
                    continue;
                }
            }
            // Make sure we're visible again after a transient minimise.
            if (target_active && !IsWindowVisible(hwnd)) {
                ShowWindow(hwnd, SW_SHOWNOACTIVATE);
            }
        } else {
            // Track which non-tubelight window currently has OS focus so
            // "Track foreground" (button or Ctrl+Alt+T) can attach to it.
            HWND fg = GetForegroundWindow();
            if (fg && fg != hwnd) last_external_foreground = fg;
        }

        apply_pending_resize();
        bool new_frame = false;
        if (!state.freeze) {
            // DXGI only delivers frames when the desktop changes.
            // Until we have a first frame, block longer so the screen
            // isn't pure black while we wait for the next natural
            // redraw (cursor blink, DWM tick, etc). After that, poll
            // with a near-zero timeout so we add only ~1 ms of capture
            // lag on top of DXGI's inherent ~1-frame compositor latency.
            // The vsync swap below caps the loop to monitor refresh, so
            // the small timeout doesn't spin a CPU core.
            DWORD timeout = have_initial ? 1 : 250;
            if (!grab_source(capture, mag_capture, new_frame, timeout)) {
                std::fprintf(stderr, "[overlay] capture lost â€” full re-init...\n");
                capture.shutdown();
                if (!capture.init(opts.monitor_index)) {
                    std::fprintf(stderr, "[overlay] capture re-init failed\n");
                    break;
                }
                continue;
            }

            // If half a second in we still have nothing, forcibly invalidate
            // the entire desktop so Windows repaints and DXGI emits a frame.
            if (!have_initial && !kicked_repaint &&
                std::chrono::steady_clock::now() - loop_started > std::chrono::milliseconds(500)) {
                std::fprintf(stderr, "[overlay] no initial frame yet â€” forcing desktop repaint\n");
                ::InvalidateRect(nullptr, nullptr, TRUE);
                ::RedrawWindow(nullptr, nullptr, nullptr,
                               RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
                kicked_repaint = true;
            }
        }

        if (new_frame || (bench && have_initial)) {
            int wx, wy, ww, wh;
            read_window_rect_on_monitor(wx, wy, ww, wh);
            // Time only the upload work (memcpy into sub_buffer + glTexSubImage2D)
            // — the real capture→GPU cost, comparable to DX12's zero-copy wrap.
            const auto up_t0 = std::chrono::high_resolution_clock::now();
            upload_subregion_to_source(capture, source, wx, wy, ww, wh,
                                        win_w, win_h, sub_buffer,
                                        fullscreen_active);
            if (bench) {
                cap_ms.push_back(std::chrono::duration<double, std::milli>(
                    std::chrono::high_resolution_clock::now() - up_t0).count());
            }
            have_initial = true;
            if (new_frame) ++frames_new;

            // Cheap luminance sample shared between audio flyback
            // modulation and the pipeline's voltage-bloom uniform.
            // sub_buffer is BGRA8. We scan 1 in 64 pixels â€” ~140 KB
            // per 1920Ã—1200 frame, sub-ms, mean within ~3 % of truth.
            if (!sub_buffer.empty()) {
                const uint8_t* px = sub_buffer.data();
                const size_t total_px = static_cast<size_t>(win_w) * win_h;
                if (total_px > 0) {
                    const size_t step = 64;
                    uint64_t acc = 0;
                    size_t   n   = 0;
                    for (size_t i = 0; i < total_px; i += step) {
                        const uint8_t* p = px + i * 4;
                        acc += static_cast<uint64_t>(p[2]) * 54
                             + static_cast<uint64_t>(p[1]) * 183
                             + static_cast<uint64_t>(p[0]) * 19;
                        ++n;
                    }
                    if (n > 0) {
                        float lum = static_cast<float>(acc) / static_cast<float>(n) / (256.0f * 255.0f);
                        if (audio_enabled && crt_audio.is_enabled()) {
                            crt_audio.set_frame_luminance(lum);
                        }
                        pipeline.set_frame_mean_luminance(lum);
                    }
                }
            }
        }
        if (bench && static_cast<int>(cap_ms.size()) >= opts.bench_frames) {
            capture_bench_report("gl", cap_ms);
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        ++frames_total;

        // Render the pipeline (or clear black if we don't have a frame yet).
        if (!have_initial) {
            glClearColor(0.02f, 0.0f, 0.02f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
        } else {
            pipeline.set_time(static_cast<float>(glfwGetTime() - t0));
            pipeline.render_to_screen(source.id());
        }

        // In-app menu (compiled-in optional).
        // Phase 2a (ADR-0002): skip the full ImGui::NewFrame / Render
        // cycle when no UI element is actually visible. begin_frame()
        // costs ~30-50µs and end_frame_to_screen() another ~50-100µs
        // for an empty draw list; cheap individually but pure waste
        // when nothing's on screen. Activate the cycle only when the
        // menu is open, the HUD is shown, a toast is still on, or we
        // need the REC dot.
        const auto now_for_ui = std::chrono::steady_clock::now();
        const bool toast_active_now = !toast_text.empty() &&
                                       (now_for_ui - toast_time < kToastShown);
        const bool ui_visible = (has_menu && menu.is_open())
                              || hud_visible
                              || toast_active_now
                              || video_recorder.is_recording();
        if (has_menu && ui_visible) {
            menu.begin_frame();
            bool want_quit_from_menu = false;
            std::string prev_profile = current_profile_id;
            std::string prev_signal  = current_signal_id;
            float       prev_intensity = intensity_multiplier;
            bool cap_changed = false;
            WindowActions wa;
            wa.is_fullscreen      = fullscreen_active;
            wa.is_tracking_target = target_active;
            wa.target_title       = target_title_cached;
            wa.is_region_active   = region_active;
            bool hud_changed = false;
            bool audio_changed = false;
            bool clickthrough_changed = false;
            bool record_changed = false;
            bool low_latency_changed = false;
            bool recordable_changed = false;
            Menu::SettingsIO sio{
                hud_visible, hud_changed,
                audio_enabled, audio_volume, audio_changed,
                clickthrough_user, clickthrough_changed,
                rec_source, rec_rx, rec_ry, rec_rw, rec_rh, record_changed,
                low_latency, low_latency_changed,
                recordable, recordable_changed,
            };
            menu.build_widgets(pipeline, current_profile_id, current_signal_id,
                               intensity_multiplier, want_quit_from_menu,
                               ui_capture_dir, cap_changed, wa, sio);
            bool any_setting_changed = false;
            if (hud_changed)            { settings.hud_visible       = hud_visible;       any_setting_changed = true; }
            if (audio_changed) {
                settings.crt_audio_enabled = audio_enabled;
                settings.crt_audio_volume  = audio_volume;
                crt_audio.set_enabled(audio_enabled);
                crt_audio.set_volume(audio_volume);
                any_setting_changed = true;
            }
            if (clickthrough_changed) {
                // The menu wrote directly to clickthrough_user; sync.
                apply_clickthrough_user(clickthrough_user);
                settings.clickthrough_user = clickthrough_user;
                any_setting_changed = true;
                // When activating click-through, auto-close the menu so
                // the user sees the effect immediately â€” otherwise the
                // menu_is_open guard keeps the overlay opaque and the
                // user thinks nothing happened.
                if (clickthrough_user) menu.set_open(false);
            }
            if (record_changed) {
                settings.record_source = rec_source;
                settings.record_rect_x = rec_rx;
                settings.record_rect_y = rec_ry;
                settings.record_rect_w = rec_rw;
                settings.record_rect_h = rec_rh;
                any_setting_changed = true;
            }
            if (low_latency_changed) {
                settings.low_latency = low_latency;
                glfwSwapInterval(low_latency ? 0 : 1);
                any_setting_changed = true;
            }
            if (recordable_changed) {
                settings.recordable = recordable;
                g_recordable_mode.store(recordable);
                if (recordable) {
                    if (!mag_capture.is_initialized()) {
                        mag_capture.init(opts.monitor_index, hwnd,
                                          GetModuleHandleW(nullptr));
                    }
                } else {
                    if (mag_capture.is_initialized()) mag_capture.shutdown();
                }
                apply_capture_affinity(hwnd);
                any_setting_changed = true;
                toast_text = recordable
                    ? "RECORDABLE: ON  (overlay live + visible to Snipping Tool / Game Bar / OBS)"
                    : "RECORDABLE: OFF (external recorders won't see overlay)";
                toast_time = std::chrono::steady_clock::now();
            }
            if (any_setting_changed) save_settings(settings);
            if (cap_changed) {
                settings.capture_dir = ui_capture_dir;
                effective_capture_dir =
                    ui_capture_dir.empty() ? default_capture_dir() : ui_capture_dir;
                save_settings(settings);
                toast_text = ui_capture_dir.empty()
                    ? std::string("Capture folder reset to default")
                    : std::string("Capture folder: ") + effective_capture_dir;
                toast_time = std::chrono::steady_clock::now();
            }

#ifdef TUBELIGHT_HAS_IMGUI
            // ---- Overlay-on-overlay HUD: toast + REC indicator ----
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            auto now_steady = std::chrono::steady_clock::now();
            if (!toast_text.empty() && (now_steady - toast_time) < kToastShown) {
                // Fade out the last 600 ms.
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                now_steady - toast_time).count();
                float alpha = 1.0f;
                auto total_ms = kToastShown.count();
                if (elapsed > total_ms - 600) {
                    alpha = std::max(0.0f,
                                static_cast<float>(total_ms - elapsed) / 600.0f);
                }
                ImVec2 ts = ImGui::CalcTextSize(toast_text.c_str());
                const float pad = 14.0f;
                ImVec2 box_min(20.0f, static_cast<float>(win_h) - ts.y - 2 * pad - 20.0f);
                ImVec2 box_max(box_min.x + ts.x + 2 * pad,
                               box_min.y + ts.y + 2 * pad);
                ImU32 col_bg   = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.78f * alpha));
                ImU32 col_text = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, alpha));
                fg->AddRectFilled(box_min, box_max, col_bg, 6.0f);
                fg->AddText(ImVec2(box_min.x + pad, box_min.y + pad),
                            col_text, toast_text.c_str());
            }
            if (video_recorder.is_recording()) {
                // Blinking red dot + REC label, top-left.
                float t = static_cast<float>(std::fmod(glfwGetTime() * 1.5, 1.5));
                float bright = (t < 1.0f) ? 1.0f : 0.4f;
                ImU32 col = ImGui::GetColorU32(ImVec4(1.0f * bright, 0.1f, 0.1f, 0.95f));
                fg->AddCircleFilled(ImVec2(28.0f, 28.0f), 9.0f, col, 24);
                fg->AddText(ImVec2(46.0f, 19.0f),
                            ImGui::GetColorU32(ImVec4(1.0f, 0.6f, 0.6f, 0.95f)),
                            "REC");
            }

            // ---- Status HUD (top-right) ----
            // Tells the user at a glance what profile/signal/mode is
            // active without having to open the menu. Especially useful
            // while target-tracking (click-through) when the menu is
            // hard to summon. Toggle: Ctrl+Alt+H or the menu checkbox.
            if (hud_visible) {
                std::string mode_line;
                if (target_active) {
                    mode_line = "Mode: Tracking";
                    if (!target_title_cached.empty()) {
                        std::string t = target_title_cached;
                        if (t.size() > 32) t = t.substr(0, 29) + "...";
                        mode_line += " \"" + t + "\"";
                    }
                } else if (region_active) {
                    mode_line = "Mode: Region (fixed rect)";
                } else if (fullscreen_active) {
                    mode_line = "Mode: Fullscreen";
                } else {
                    mode_line = "Mode: Windowed";
                    if (clickthrough_user) mode_line += " (click-through)";
                }
                if (recordable) mode_line += " [rec-able]";

                std::string profile_line = "Profile: " +
                    (current_profile_id.empty() ? std::string("(default)")
                                                : current_profile_id);
                bool mono_now = (pipeline.params().monochrome == 1);
                std::string signal_line = mono_now
                    ? std::string("Signal:  clean RGB (mono-locked)")
                    : ("Signal:  " + (current_signal_id.empty()
                                        ? std::string("(default)")
                                        : current_signal_id));

                ImVec2 sz_p = ImGui::CalcTextSize(profile_line.c_str());
                ImVec2 sz_s = ImGui::CalcTextSize(signal_line.c_str());
                ImVec2 sz_m = ImGui::CalcTextSize(mode_line.c_str());
                float text_w = std::max({sz_p.x, sz_s.x, sz_m.x});
                float text_h = sz_p.y + sz_s.y + sz_m.y;
                const float pad = 10.0f;
                ImVec2 box_max(static_cast<float>(win_w) - 16.0f, 16.0f + text_h + 2 * pad);
                ImVec2 box_min(box_max.x - text_w - 2 * pad, 16.0f);

                ImU32 col_bg   = ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.65f));
                ImU32 col_text = ImGui::GetColorU32(ImVec4(0.85f, 1.0f, 0.85f, 0.95f));
                fg->AddRectFilled(box_min, box_max, col_bg, 6.0f);

                float y = box_min.y + pad;
                fg->AddText(ImVec2(box_min.x + pad, y), col_text, profile_line.c_str()); y += sz_p.y;
                fg->AddText(ImVec2(box_min.x + pad, y), col_text, signal_line.c_str()); y += sz_s.y;
                fg->AddText(ImVec2(box_min.x + pad, y), col_text, mode_line.c_str());
            }
#endif

            menu.end_frame_to_screen();

            if (want_quit_from_menu) glfwSetWindowShouldClose(window, GLFW_TRUE);

            if (current_profile_id != prev_profile && !current_profile_id.empty()) {
                std::string err;
                auto p = tubelight::load_crt_profile_by_id(current_profile_id, err);
                if (p) {
                    pipeline.apply_crt_profile(*p);
                    base_params = pipeline.params();
                    intensity_multiplier = 1.0f;
                    // Auto-load a per-profile bezel PNG if it exists.
                    // Searched at assets/bezels/<id>.png next to the exe.
                    // PNG should have alpha=0 inside the screen area and
                    // alpha=1 (opaque) over the monitor casing.
                    {
                        std::string bezel_path =
                            std::string("assets/bezels/") + current_profile_id + ".png";
                        if (!pipeline.load_bezel_image(bezel_path)) {
                            pipeline.clear_bezel_image();
                        }
                    }
                    // Trigger degauss thump on profile switch â€” that
                    // characteristic low rumble a CRT makes when you
                    // change input or turn it on.
                    crt_audio.trigger_degauss(1.0f);
                    std::fprintf(stderr,
                        "[overlay] applied CRT '%s': mono=%d phosphor=(%.2f,%.2f,%.2f) tint=(%.2f,%.2f,%.2f) age=%.2f mask=%d strength=%.2f\n",
                        current_profile_id.c_str(),
                        pipeline.params().monochrome,
                        pipeline.params().phosphor_color_r,
                        pipeline.params().phosphor_color_g,
                        pipeline.params().phosphor_color_b,
                        pipeline.params().glass_tint_r,
                        pipeline.params().glass_tint_g,
                        pipeline.params().glass_tint_b,
                        pipeline.params().glass_age,
                        pipeline.params().mask_type,
                        pipeline.params().mask_strength);
                } else {
                    std::fprintf(stderr, "[overlay] could not load CRT '%s': %s\n",
                                 current_profile_id.c_str(), err.c_str());
                }
            }
            if (current_signal_id != prev_signal && !current_signal_id.empty()) {
                std::string err;
                auto s = tubelight::load_signal_profile_by_id(current_signal_id, err);
                if (s) {
                    pipeline.apply_signal_profile(*s);
                    // apply_signal_profile mutates params (notably
                    // scanline_count and possibly more). Re-snapshot
                    // base_params so the intensity-scale path keeps
                    // operating on the *current* baseline.
                    base_params = pipeline.params();
                    intensity_multiplier = 1.0f;
                    // Pick the right flyback frequency for the audio
                    // synth so PAL signals get 15.625 kHz instead of
                    // 15.734 kHz NTSC. Approx h_freq_khz Ã— 1000.
                    crt_audio.set_flyback_frequency_hz(
                        static_cast<float>(s->h_freq_khz) * 1000.0f);
                    std::fprintf(stderr, "[overlay] applied signal '%s'\n",
                                 current_signal_id.c_str());
                } else {
                    std::fprintf(stderr, "[overlay] could not load signal '%s': %s\n",
                                 current_signal_id.c_str(), err.c_str());
                }
            }
            if (intensity_multiplier != prev_intensity) {
                auto& P = pipeline.params();
                float k = intensity_multiplier;
                P.scanline_strength = std::min(1.0f,  base_params.scanline_strength * k);
                P.mask_strength     = std::min(1.0f,  base_params.mask_strength     * k);
                P.bloom_strength    = std::min(1.0f,  base_params.bloom_strength    * k);
                P.halation_strength = std::min(1.0f,  base_params.halation_strength * k);
                P.barrel_strength   = std::min(0.20f, base_params.barrel_strength   * k);
                P.vignette_strength = std::min(1.0f,  base_params.vignette_strength * k);
            }

            // Re-apply aspect lock if the CRT profile changed (its
            // aspect_native field updates target_aspect) or the user
            // picked a new aspect via the combo.
            apply_aspect_lock();

            if (wa.snap_to_aspect_requested)    do_snap_to_aspect();
            if (wa.toggle_fullscreen_requested) do_toggle_fullscreen();
            if (wa.detach_target_requested)     do_detach_target();
            if (wa.track_foreground_requested && last_external_foreground) {
                do_attach_target(last_external_foreground);
            }
            if (wa.track_by_title_requested && !wa.title_to_track.empty()) {
                HWND found = find_target_hwnd(wa.title_to_track, 0, hwnd);
                if (found) {
                    do_attach_target(found);
                } else {
                    toast_text = "No window found matching '" + wa.title_to_track + "'";
                    toast_time = std::chrono::steady_clock::now();
                }
            }
            if (wa.region_attach_requested) {
                do_attach_region(wa.region_x, wa.region_y, wa.region_w, wa.region_h);
            }
            if (wa.region_detach_requested) {
                do_detach_region();
            }
            if (wa.save_preset_requested) {
                std::string err;
                std::string base = current_profile_id.empty() ? "pvm-8220" : current_profile_id;
                if (save_crt_preset(base, wa.preset_new_id, wa.preset_display_name,
                                    pipeline.params(), err)) {
                    menu.invalidate_profile_cache();  // refresh combo on next open
                    toast_text = "Preset saved: " + wa.preset_new_id;
                    toast_time = std::chrono::steady_clock::now();
                } else {
                    toast_text = "Preset save failed: " + err;
                    toast_time = std::chrono::steady_clock::now();
                }
            }
        }

        glfwSwapBuffers(window);
        glfwPollEvents();

        // Hard frame-rate cap when vsync is off. Without this the loop
        // spins at thousands of FPS, pegging one CPU core at 100 % and
        // the GPU at maximum â€” which on some systems drags the entire
        // desktop down to a few FPS (the regression the user reported).
        // Cap at 240 FPS by default (4.16 ms / frame), still very
        // low-latency but leaves the system breathing room.
        if (low_latency) {
            static auto last_frame = std::chrono::steady_clock::now();
            auto target = last_frame + std::chrono::microseconds(4166);
            auto now = std::chrono::steady_clock::now();
            if (now < target) std::this_thread::sleep_until(target);
            last_frame = std::chrono::steady_clock::now();
        }

        // Apply any hotkey signals fed by the low-level keyboard hook.
        if (g_hk_quit.load()) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        if (g_hk_freeze_toggle.exchange(false)) {
            state.freeze = !state.freeze;
            std::printf("[overlay] freeze: %s\n", state.freeze ? "ON" : "OFF");
        }
        if (g_hk_all_on.exchange(false)) {
            for (int i = 0; i < tubelight::Pipeline::kPassCount; ++i)
                pipeline.set_pass_enabled(i, true);
            std::printf("[overlay] all passes ON\n");
        }
        int p = g_hk_toggle_pass.exchange(-1);
        if (p >= 0 && p < tubelight::Pipeline::kPassCount) {
            bool cur = pipeline.is_pass_enabled(p);
            pipeline.set_pass_enabled(p, !cur);
            std::printf("[overlay] %s: %s\n",
                        tubelight::pass_display_name(p), !cur ? "ON" : "OFF");
        }
        if (g_hk_toggle_menu.exchange(false) && has_menu) {
            menu.toggle();
        }
        if (g_hk_toggle_fullscreen.exchange(false)) {
            do_toggle_fullscreen();
        }
        if (g_hk_toggle_target.exchange(false)) {
            if (target_active) {
                do_detach_target();
            } else if (last_external_foreground) {
                do_attach_target(last_external_foreground);
            } else {
                std::fprintf(stderr,
                    "[overlay] Ctrl+Alt+T: no foreground window remembered yet\n");
            }
        }
        if (g_hk_toggle_hud.exchange(false)) {
            hud_visible = !hud_visible;
            settings.hud_visible = hud_visible;
            save_settings(settings);
        }
        // ADR-0001: g_hk_toggle_clickthrough and g_hk_toggle_recordable
        // are now no-op atomics — the LL keyboard hook no longer sets
        // them and no other code path reaches them. They're left in
        // place for ABI compatibility with grab_source / menu readers.
        // The .exchange(false) discards is intentionally absent so the
        // CPU doesn't run an atomic write on every frame for dead flags.
        // Screenshot: read the framebuffer AFTER the pipeline rendered but
        // BEFORE swap, so we capture exactly what the user sees. The PNG
        // zlib encode runs on a background thread (at 1920x1200 in
        // fullscreen it would otherwise block the main loop for ~1-2 s,
        // which the user perceives as a freeze).
        if (g_hk_screenshot.exchange(false)) {
            std::string err;
            std::string out = save_screenshot_png_async(win_w, win_h,
                                                         effective_capture_dir, err);
            if (!out.empty()) {
                auto slash = out.find_last_of("/\\");
                std::string fname = (slash == std::string::npos) ? out : out.substr(slash + 1);
                toast_text = "Screenshot saving: " + fname;
                toast_time = std::chrono::steady_clock::now();
            } else {
                std::fprintf(stderr, "[overlay] screenshot failed: %s\n", err.c_str());
                toast_text = "Screenshot failed";
                toast_time = std::chrono::steady_clock::now();
            }
        }
        if (g_hk_toggle_video.exchange(false)) {
            if (video_recorder.is_recording()) {
                video_recorder.stop();
                std::fprintf(stderr, "[overlay] video saved: %s\n",
                              video_recorder.output_path().c_str());
                auto slash = video_recorder.output_path().find_last_of("/\\");
                std::string fname = (slash == std::string::npos)
                    ? video_recorder.output_path()
                    : video_recorder.output_path().substr(slash + 1);
                toast_text = "Video saved: " + fname;
                toast_time = std::chrono::steady_clock::now();
            } else {
                std::string err;
                int rw_rec, rh_rec;
                if (rec_source == 1) {
                    rw_rec = capture.width();
                    rh_rec = capture.height();
                } else if (rec_source == 2) {
                    rw_rec = std::max(rec_rw, 16);
                    rh_rec = std::max(rec_rh, 16);
                } else {
                    rw_rec = win_w;
                    rh_rec = win_h;
                }
                if (video_recorder.start(rw_rec, rh_rec, 60,
                                         effective_capture_dir, err)) {
                    std::fprintf(stderr, "[overlay] video recording â†’ %s\n",
                                  video_recorder.output_path().c_str());
                    toast_text = "Recording... Ctrl+Alt+V to stop";
                    toast_time = std::chrono::steady_clock::now();
                } else {
                    std::fprintf(stderr, "[overlay] video start failed: %s\n",
                                  err.c_str());
                    toast_text = "Video start failed (is ffmpeg in PATH?)";
                    toast_time = std::chrono::steady_clock::now();
                }
            }
        }
        if (video_recorder.is_recording()) {
            bool pushed = false;
            if (rec_source == 0) {
                // Overlay view (CRT-effect, what you see on the back buffer).
                pushed = video_recorder.push_frame();
            } else if (rec_source == 1) {
                // Full monitor: raw DXGI BGRA. No CRT effect on output.
                pushed = video_recorder.push_frame_from_bgra(
                    capture.pixels(), capture.width(), capture.height(),
                    0, 0);
            } else {
                // Custom monitor-relative rect.
                pushed = video_recorder.push_frame_from_bgra(
                    capture.pixels(), capture.width(), capture.height(),
                    rec_rx, rec_ry);
            }
            if (!pushed) {
                // Pipe broken (ffmpeg crashed / disk full). Bail out and tell
                // the user â€” we don't sit forever in zombie-recording mode.
                video_recorder.stop();
                toast_text = "Recording stopped: pipe error";
                toast_time = std::chrono::steady_clock::now();
            }
        }

        // When the menu is open we need clicks to land on it, so we drop
        // Menu open/close: temporarily disable WS_EX_TRANSPARENT while
        // the menu is up so the user can click on widgets. Restore on
        // close if click-through is supposed to stay on.
        bool menu_open_now = has_menu && menu.is_open();
        const bool should_be_click_through =
            initial_fullscreen || target_active || region_active || clickthrough_user;
        if (menu_open_now != menu_was_open) {
            LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            if (menu_open_now) {
                ex &= ~WS_EX_TRANSPARENT;
                SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
                SetForegroundWindow(hwnd);
                SetFocus(hwnd);
            } else if (should_be_click_through) {
                ex |= WS_EX_TRANSPARENT;
                SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
            }
            menu_was_open = menu_open_now;
        }

        // Periodic health log.
        if ((frames_total % 300) == 0) {
            std::fprintf(stderr, "[overlay] %llu frames rendered (%llu new from DXGI)\n",
                         frames_total, frames_new);
        }
    }

    // Stop the hotkey thread.
    PostThreadMessage(hk_tid.load(), WM_QUIT, 0, 0);
    if (hotkey_thread.joinable()) hotkey_thread.join();

    if (has_menu) menu.shutdown();
    if (video_recorder.is_recording()) video_recorder.stop();

    // Nullify GLFW callbacks and user pointer before tearing down â€” the
    // framebuffer-size callback can fire one last time during destruction
    // and our stack-local AppState/ResizeState would already be gone.
    glfwSetFramebufferSizeCallback(window, nullptr);
    glfwSetKeyCallback(window, nullptr);
    glfwSetWindowUserPointer(window, nullptr);

    // Restore the original WndProc and drop the per-window render-state
    // pointer before the GLFW window is destroyed.
    if (g_orig_wndproc) {
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC,
                          reinterpret_cast<LONG_PTR>(g_orig_wndproc));
        g_orig_wndproc = nullptr;
    }
    KillTimer(hwnd, kModalRenderTimerId);
    RemovePropW(hwnd, kFrameRenderProp);

    capture.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

} // namespace tubelight::overlay
