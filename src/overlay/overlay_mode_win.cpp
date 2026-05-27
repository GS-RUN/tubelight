// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// Windows implementation of the overlay mode.
//
// Strategy (the "no-injection" path; latency ~1 frame, always works):
//   1. Create a fullscreen borderless GLFW window with OpenGL 4.5 core,
//      WS_EX_TOPMOST + WS_EX_NOACTIVATE so the user keeps interacting with
//      whatever is underneath.
//   2. Call SetWindowDisplayAffinity(WDA_EXCLUDEFROMCAPTURE) so DXGI does
//      NOT see the overlay itself in its captures — otherwise we'd grab
//      our own output and feedback.
//   3. Set up DXGI Desktop Duplication on the chosen monitor.
//   4. Each frame: AcquireNextFrame (with 0 ms timeout — re-use previous
//      capture if nothing changed), CopyResource into a CPU-readable
//      staging texture, Map, memcpy the BGRA8 bytes into a CPU buffer.
//   5. glTexSubImage2D into the OpenGL source texture; flip BGRA→RGBA via
//      a sampler swizzle (TEXTURE_SWIZZLE_RGBA).
//   6. Run the pipeline; the output fills the full overlay window.
//
// Keys:
//   ESC                quit
//   1..8               toggle individual passes
//   0                  re-enable all passes
//   F                  toggle "freeze" — keep last capture
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
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
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
std::atomic<bool> g_hk_toggle_clickthrough{false};
std::atomic<bool> g_hk_toggle_recordable{false};

// True when the user has turned on "recordable" mode (Ctrl+Alt+R or the
// menu checkbox). Read by apply_capture_affinity() so every code path
// that re-asserts the affinity flag honours the current state. Sticky
// across mode changes (fullscreen ↔ windowed ↔ target ↔ region).
std::atomic<bool> g_recordable_mode{false};

// Centralised wrapper: pick WDA_NONE (overlay shows up in external
// captures, used while the user is recording with Snipping Tool / Game
// Bar / OBS) vs WDA_EXCLUDEFROMCAPTURE (the default — prevents DXGI
// Desktop Duplication from feedback-looping the overlay's own output).
inline void apply_capture_affinity(HWND hwnd) {
    SetWindowDisplayAffinity(hwnd,
        g_recordable_mode.load() ? WDA_NONE : WDA_EXCLUDEFROMCAPTURE);
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
        else if (vk == 'C') {
            g_hk_toggle_clickthrough = true;
            std::fprintf(stderr, "[overlay] LL hook fired Ctrl+Alt+C\n");
        }
        else if (vk == 'R')                       g_hk_toggle_recordable = true;
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
// user grabs the title bar runs entirely inside DefWindowProc — glfwPollEvents
// never returns — so the captured frame freezes until the user releases the
// mouse. WM_ENTERSIZEMOVE → start a ~16 ms WM_TIMER that drives one render
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
// (title bar + borders → 0 pixels). Used by the runtime fullscreen toggle
// to go borderless WITHOUT touching GWL_STYLE / WS_EX_LAYERED — those style
// swaps drop WDA_EXCLUDEFROMCAPTURE on this hardware (NVIDIA + Win11), which
// in turn breaks DXGI Desktop Duplication's exclusion of our own overlay
// and produces the recursive feedback ghost effect.
std::atomic<bool> g_hide_nonclient{false};

// When true, the subclass proc returns HTTRANSPARENT for every hit-test
// query, causing Windows to deliver mouse events to whatever's underneath
// our overlay (i.e. true click-through) WITHOUT requiring WS_EX_LAYERED
// or WS_EX_TRANSPARENT — those have to interact with OpenGL composition
// in fiddly ways. NCHITTEST is the clean low-level path: independent of
// style bits, takes effect on the very next click.
std::atomic<bool> g_clickthrough_effective{false};

LRESULT CALLBACK tubelight_subclass_proc(HWND hwnd, UINT msg,
                                         WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_NCHITTEST:
        if (g_clickthrough_effective.load()) {
            // Tell Windows "this pixel is transparent" → the click is
            // re-routed to whatever window is behind us in z-order.
            // Works without WS_EX_LAYERED, doesn't fight OpenGL.
            static std::atomic<int> log_once{0};
            if (log_once.exchange(1) == 0) {
                std::fprintf(stderr, "[overlay] WM_NCHITTEST → HTTRANSPARENT (click-through active)\n");
            }
            return HTTRANSPARENT;
        }
        break;
    case WM_MOUSEACTIVATE:
        if (g_clickthrough_effective.load()) {
            return MA_NOACTIVATEANDEAT;
        }
        break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        // Fallback path for the global hotkeys when the LL hook is being
        // intercepted by another driver (NVIDIA, Logitech, antivirus,
        // some screen-grabber tools insert themselves first). The window
        // sees these directly when focused. We only fire the click-through
        // toggle here since that's the one users have reported as broken;
        // the other hotkeys are less likely to need a fallback.
        const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool alt  = (GetAsyncKeyState(VK_MENU)    & 0x8000) != 0;
        if (ctrl && alt && wp == 'C') {
            g_hk_toggle_clickthrough = true;
            std::fprintf(stderr, "[overlay] WndProc fallback fired Ctrl+Alt+C\n");
            return 0;
        }
        break;
    }
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

        // 2) Enumerate adapter → output → duplication.
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
            std::fprintf(stderr, "[overlay] ACCESS_LOST — recreating duplication...\n");
            dup_.Reset();
            return reacquire_duplication();
        }
        if (FAILED(hr)) {
            std::fprintf(stderr, "[overlay] AcquireNextFrame failed: 0x%08lx\n", hr);
            return false;
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
        // Walks adapter → output again and creates a fresh duplication.
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
};

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

// Extracts the rect (mon_x, mon_y, mon_w, mon_h) — relative to monitor 0,0
// — from the DXGI capture and uploads it to `source`, vertically flipped
// (DXGI top-down → GL bottom-up). For fullscreen mode the rect is ignored
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
        // Identity path: full desktop, top-down → bottom-up.
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

} // namespace

// ---------------------------------------------------------------------------
// Public entry
// ---------------------------------------------------------------------------

int run(const Options& opts) {
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

    // Window mode: Windowed = movable resizable normal Win32 window with a
    // title bar; Fullscreen = borderless topmost covering the whole monitor;
    // Region / TargetWindow = positioned + sized to track a user rectangle
    // or another application's window.
    //
    // `initial_fullscreen` captures the launch-time CLI mode and never
    // changes; it gates the click-through (WS_EX_TRANSPARENT) behaviour the
    // standalone --overlay-fullscreen mode needs. `fullscreen_active`
    // mirrors it on startup but the in-app menu / Ctrl+Alt+Enter can flip
    // it at runtime — that runtime fullscreen is *focusable* so the user
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
    // in DXGI vs bottom-left in GL — handled by feeding rows in normal order
    // and reading texture(uv) with uv.y = 1 - uv.y; simplest fix: we keep DXGI
    // top-down rows AND ALSO compensate by using GL_REPEAT? Instead we flip
    // here by uploading with a temp buffer when needed — see below).

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
            std::printf("[overlay] CRT profile: %s\n", p->display_name.c_str());
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

    // Toast state — declared early so apply_clickthrough_user can write to it.
    std::string toast_text;
    auto toast_time = std::chrono::steady_clock::now() - std::chrono::hours(1);
    const auto kToastShown = std::chrono::milliseconds(2500);

    auto apply_clickthrough_user = [&](bool on) {
        clickthrough_user = on;
        // Cross-process click-through REQUIRES WS_EX_LAYERED +
        // WS_EX_TRANSPARENT (DWM honours hit-test there). WM_NCHITTEST
        // returning HTTRANSPARENT only routes clicks to windows in the
        // SAME thread — useless for "click on overlay → openMSX gets it"
        // because openMSX is a different process.
        //
        // LAYERED gets added on first activation and is then LEFT set
        // permanently — runtime removal of LAYERED is unreliable on
        // some hardware (broke earlier). We only toggle TRANSPARENT +
        // NOACTIVATE between sessions.
        LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if (on) {
            if (!(ex & WS_EX_LAYERED)) {
                ex |= WS_EX_LAYERED;
                SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
                // Set alpha=255 so the layered window paints normally
                // (OpenGL back buffer composited at full opacity).
                SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
                ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);  // re-read
            }
            ex |= WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
        } else {
            ex &= ~(WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
        }
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
        // Force the new ex style to take effect immediately.
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOACTIVATE | SWP_FRAMECHANGED);
        apply_capture_affinity(hwnd);
        std::fprintf(stderr, on ? "[overlay] click-through ON (WS_EX_TRANSPARENT)\n"
                                : "[overlay] click-through OFF\n");
        toast_text  = on
            ? "CLICK-THROUGH: ON  (clicks pass to apps below)"
            : "CLICK-THROUGH: OFF (clicks land on the overlay)";
        toast_time  = std::chrono::steady_clock::now();
    };

    // Lock GLFW's user-drag resize to match the current target aspect (if
    // any). target_aspect == 0 means "fill", so we release the constraint.
    auto apply_aspect_lock = [&]() {
        if (windowed_mode && !fullscreen_active && pipeline.params().target_aspect > 0.0f) {
            // GLFW expects a rational ratio — scaling by 10000 keeps two
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

    // Saved windowed pos/size — restored when leaving runtime fullscreen.
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
    //      WDA_EXCLUDEFROMCAPTURE → DXGI captures our overlay → recursive
    //      feedback ghost. So we use WM_NCCALCSIZE in our subclass to
    //      collapse the non-client area to 0 pixels instead.
    //   2) A topmost window that covers the *entire* monitor gets
    //      promoted by Win11 to Independent Flip / direct scanout, which
    //      bypasses DWM compositing → WDA stops applying → same ghost.
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

            // Hide non-client (NCCALCSIZE→0) + size to monitor minus 1px
            // at the bottom to dodge Win11 Independent-Flip promotion.
            g_hide_nonclient = true;
            SetWindowPos(hwnd, HWND_TOPMOST,
                         capture.origin_x(), capture.origin_y(),
                         capture.width(),    capture.height() - 1,
                         SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW);
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

        SetWindowPos(hwnd, HWND_TOPMOST,
                     capture.origin_x() + rx, capture.origin_y() + ry, rw, rh,
                     SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_SHOWWINDOW);
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

    // Menu state — selected profile/signal ids + a global intensity multiplier.
    std::string current_profile_id = opts.profile_id;
    std::string current_signal_id  = opts.signal_id;
    float intensity_multiplier     = 1.0f;
    Pipeline::GlobalParams base_params = pipeline.params();

    Settings settings = load_settings();
    // Migration: the previous build defaulted low_latency=true which
    // caused the render loop to spin without bound and starve the
    // rest of the desktop. Force it off once on this version so users
    // upgrading don't keep the bad state.
    if (settings.low_latency) {
        std::fprintf(stderr, "[overlay] migrating settings.low_latency true → false (was making the desktop lag)\n");
        settings.low_latency = false;
        save_settings(settings);
    }
    // Migration: click-through-at-startup was footgun — if it persists
    // ON, the user can't interact with the title bar / X button on the
    // next launch and gets stuck (Ctrl+Alt+M / Ctrl+Alt+C still rescue,
    // but it's still surprising). Force off + don't persist between
    // sessions. It stays a per-session toggle from now on.
    if (settings.clickthrough_user) {
        std::fprintf(stderr, "[overlay] migrating settings.clickthrough_user true → false (no longer auto-applied at startup)\n");
        settings.clickthrough_user = false;
        save_settings(settings);
    }
    std::string effective_capture_dir =
        settings.capture_dir.empty() ? default_capture_dir() : settings.capture_dir;
    std::string ui_capture_dir = settings.capture_dir;
    bool hud_visible    = settings.hud_visible;
    bool  audio_enabled = settings.crt_audio_enabled;
    float audio_volume  = settings.crt_audio_volume;
    bool  low_latency   = settings.low_latency;
    bool  recordable    = settings.recordable;
    g_recordable_mode.store(recordable);
    // The initial WDA_EXCLUDEFROMCAPTURE was applied during window setup
    // before settings were loaded; re-apply now in case the user had
    // recordable=true persisted from a previous run.
    apply_capture_affinity(hwnd);
    glfwSwapInterval(low_latency ? 0 : 1);
    int   rec_source    = settings.record_source;
    int   rec_rx        = settings.record_rect_x;
    int   rec_ry        = settings.record_rect_y;
    int   rec_rw        = settings.record_rect_w;
    int   rec_rh        = settings.record_rect_h;
    // Apply persisted click-through at startup (windowed mode only).
    // Click-through is now strictly a per-session toggle — never auto-
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
                          : "built without imgui — menu disabled");

    LONG_PTR base_ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    (void)base_ex_style;
    bool menu_was_open = false;

    std::printf(
        "[overlay] %dx%d — capturing first desktop frame before showing window...\n",
        W, H);

    // upload_subregion_to_source() does the vertical flip into `sub_buffer`
    // (resized by the framebuffer-size callback) — no separate full-frame
    // buffer is needed any more.

    // -----------------------------------------------------------------
    // Install a low-level keyboard hook so Ctrl+Alt+<key> works regardless
    // of focus and regardless of any other app having registered the same
    // hotkey via RegisterHotKey. The hook runs on the thread that called
    // SetWindowsHookEx, but Windows uses an internal message dispatch so
    // we don't strictly need a separate pump — however a worker thread
    // with its own GetMessage loop is the canonical pattern, which we use
    // here to avoid interference with GLFW's own message handling.
    // -----------------------------------------------------------------
    g_hk_quit = false;
    g_hk_freeze_toggle = false;
    g_hk_all_on = false;
    g_hk_toggle_pass = -1;
    g_hk_toggle_recordable = false;

    std::atomic<DWORD> hk_tid{0};
    std::atomic<HHOOK> hk_handle{nullptr};

    std::thread hotkey_thread([&]() {
        hk_tid = GetCurrentThreadId();
        HHOOK h = SetWindowsHookEx(WH_KEYBOARD_LL, kb_hook_proc,
                                    GetModuleHandleW(nullptr), 0);
        if (!h) {
            std::fprintf(stderr,
                "[overlay] SetWindowsHookEx failed: GLE=%lu — Ctrl+Alt+Q won't work\n",
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
            if (!capture.grab(new_frame, 500)) {
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
        "Ctrl+Alt+R recordable (external recorders) | "
        "Ctrl+Alt+H toggle HUD | Ctrl+Alt+S screenshot | "
        "Ctrl+Alt+V video\n"
        "[overlay] (debug) Ctrl+Alt+0 all passes on | Ctrl+Alt+1..8 toggle individual pass\n");

    double t0 = glfwGetTime();
    bool have_initial = true;
    unsigned long long frames_total = 0, frames_new = 0;
    bool kicked_repaint = false;
    auto loop_started = std::chrono::steady_clock::now();

    // ---------------------------------------------------------------------
    // Subclass the window so the overlay keeps rendering DURING a user
    // move / resize. Without this, the image freezes the moment the user
    // grabs the title bar — Windows enters a modal loop inside
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
            (void)capture.grab(new_frame, 0);
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
            // redraw (cursor blink, DWM tick, etc).
            DWORD timeout = have_initial ? 16 : 250;
            if (!capture.grab(new_frame, timeout)) {
                std::fprintf(stderr, "[overlay] capture lost — full re-init...\n");
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
                std::fprintf(stderr, "[overlay] no initial frame yet — forcing desktop repaint\n");
                ::InvalidateRect(nullptr, nullptr, TRUE);
                ::RedrawWindow(nullptr, nullptr, nullptr,
                               RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
                kicked_repaint = true;
            }
        }

        if (new_frame) {
            int wx, wy, ww, wh;
            read_window_rect_on_monitor(wx, wy, ww, wh);
            upload_subregion_to_source(capture, source, wx, wy, ww, wh,
                                        win_w, win_h, sub_buffer,
                                        fullscreen_active);
            have_initial = true;
            ++frames_new;

            // Cheap luminance sample shared between audio flyback
            // modulation and the pipeline's voltage-bloom uniform.
            // sub_buffer is BGRA8. We scan 1 in 64 pixels — ~140 KB
            // per 1920×1200 frame, sub-ms, mean within ~3 % of truth.
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
        if (has_menu) {
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
                // the user sees the effect immediately — otherwise the
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
                apply_capture_affinity(hwnd);
                any_setting_changed = true;
                toast_text = recordable
                    ? "RECORDABLE: ON  (Snipping Tool / Game Bar can see overlay; expect feedback ghost if not in target/region mode)"
                    : "RECORDABLE: OFF (external recorders won't see overlay)";
                toast_time = std::chrono::steady_clock::now();
                std::fprintf(stderr, "[overlay] recordable %s (WDA_%s)\n",
                             recordable ? "ON" : "OFF",
                             recordable ? "NONE" : "EXCLUDEFROMCAPTURE");
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
                    // Trigger degauss thump on profile switch — that
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
                    // 15.734 kHz NTSC. Approx h_freq_khz × 1000.
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
        // the GPU at maximum — which on some systems drags the entire
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
        if (g_hk_toggle_clickthrough.exchange(false)) {
            // Only meaningful in plain windowed mode. Fullscreen / target /
            // region already manage click-through themselves.
            if (!initial_fullscreen && !fullscreen_active && !target_active && !region_active) {
                apply_clickthrough_user(!clickthrough_user);
            }
        }
        if (g_hk_toggle_recordable.exchange(false)) {
            recordable = !recordable;
            g_recordable_mode.store(recordable);
            apply_capture_affinity(hwnd);
            settings.recordable = recordable;
            save_settings(settings);
            toast_text = recordable
                ? "RECORDABLE: ON  (Snipping Tool / Game Bar can see overlay; expect feedback ghost if not in target/region mode)"
                : "RECORDABLE: OFF (external recorders won't see overlay)";
            toast_time = std::chrono::steady_clock::now();
            std::fprintf(stderr, "[overlay] recordable %s via hotkey (WDA_%s)\n",
                         recordable ? "ON" : "OFF",
                         recordable ? "NONE" : "EXCLUDEFROMCAPTURE");
        }
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
                    std::fprintf(stderr, "[overlay] video recording → %s\n",
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
                // the user — we don't sit forever in zombie-recording mode.
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

    // Nullify GLFW callbacks and user pointer before tearing down — the
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
