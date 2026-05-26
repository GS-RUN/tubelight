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
#include "overlay/capture_to_disk.h"
#include "overlay/menu.h"
#include "overlay/settings.h"
#include "profile/profile_loader.h"

#ifdef TUBELIGHT_HAS_IMGUI
#include <imgui.h>
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
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
        else if (vk == '0' || vk == VK_NUMPAD0)   g_hk_all_on = true;
        else if (vk >= '1' && vk <= '8')          g_hk_toggle_pass = static_cast<int>(vk - '1');
        else if (vk >= VK_NUMPAD1 && vk <= VK_NUMPAD8)
                                                 g_hk_toggle_pass = static_cast<int>(vk - VK_NUMPAD1);
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
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
                                 int mon_x, int mon_y, int mon_w, int mon_h,
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
    const bool fullscreen_mode  = (opts.mode == OverlayMode::Fullscreen);
    const bool windowed_mode    = (opts.mode == OverlayMode::Windowed);

    glfwWindowHint(GLFW_VISIBLE,   GLFW_FALSE);
    glfwWindowHint(GLFW_DECORATED, windowed_mode ? GLFW_TRUE  : GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING,  windowed_mode ? GLFW_FALSE : GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, windowed_mode ? GLFW_TRUE  : GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUSED,   windowed_mode ? GLFW_TRUE  : GLFW_FALSE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, windowed_mode ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    int W = fullscreen_mode ? capture.width()  : opts.init_w;
    int H = fullscreen_mode ? capture.height() : opts.init_h;
    GLFWwindow* window = glfwCreateWindow(W, H, "Tubelight", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "[overlay] glfwCreateWindow failed\n");
        capture.shutdown();
        glfwTerminate();
        return 1;
    }

    if (fullscreen_mode) {
        glfwSetWindowPos(window, capture.origin_x(), capture.origin_y());
    } else {
        // Centre on the chosen monitor.
        glfwSetWindowPos(window,
                         capture.origin_x() + (capture.width()  - W) / 2,
                         capture.origin_y() + (capture.height() - H) / 2);
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    HWND hwnd = glfwGetWin32Window(window);

    // Exclude the overlay from screen capture (avoid feedback loop where
    // DXGI would otherwise include our own rendered output in its grab).
    SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);

    if (fullscreen_mode) {
        // Fullscreen overlay: click-through topmost so the user keeps
        // interacting with whatever is underneath.
        LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        ex |= WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
        SetWindowPos(hwnd, HWND_TOPMOST, capture.origin_x(), capture.origin_y(),
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
            std::printf("[overlay] signal profile: %s\n", s->display_name.c_str());
        } else {
            std::fprintf(stderr, "[overlay] signal profile '%s' not found: %s\n",
                         opts.signal_id.c_str(), err.c_str());
        }
    }

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
    std::string effective_capture_dir =
        settings.capture_dir.empty() ? default_capture_dir() : settings.capture_dir;
    std::string ui_capture_dir = settings.capture_dir;

    VideoRecorder video_recorder;
    std::string toast_text;
    auto toast_time = std::chrono::steady_clock::now() - std::chrono::hours(1);
    const auto kToastShown = std::chrono::milliseconds(2500);

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
                                            fullscreen_mode);
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
    if (fullscreen_mode) {
        SetWindowPos(hwnd, HWND_TOPMOST, capture.origin_x(), capture.origin_y(),
                     W, H, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    }

    std::printf(
        "[overlay] hotkeys: Ctrl+Alt+Q quit | Ctrl+Alt+F freeze | "
        "Ctrl+Alt+0 all-on | Ctrl+Alt+1..8 toggle pass\n");

    double t0 = glfwGetTime();
    bool have_initial = true;
    unsigned long long frames_total = 0, frames_new = 0;
    bool kicked_repaint = false;
    auto loop_started = std::chrono::steady_clock::now();

    while (!glfwWindowShouldClose(window)) {
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
                                        fullscreen_mode);
            have_initial = true;
            ++frames_new;
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
            menu.build_widgets(pipeline, current_profile_id, current_signal_id,
                               intensity_multiplier, want_quit_from_menu,
                               ui_capture_dir, cap_changed);
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
        }

        glfwSwapBuffers(window);
        glfwPollEvents();

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
        // Screenshot: read the framebuffer AFTER the pipeline rendered but
        // BEFORE swap, so we capture exactly what the user sees.
        if (g_hk_screenshot.exchange(false)) {
            std::string err;
            std::string out = save_screenshot_png(win_w, win_h,
                                                    effective_capture_dir, err);
            if (!out.empty()) {
                std::fprintf(stderr, "[overlay] screenshot saved: %s\n", out.c_str());
                // Trim path to just the filename for the toast.
                auto slash = out.find_last_of("/\\");
                std::string fname = (slash == std::string::npos) ? out : out.substr(slash + 1);
                toast_text = "Screenshot saved: " + fname;
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
                if (video_recorder.start(win_w, win_h, 60,
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
            if (!video_recorder.push_frame()) {
                // Pipe broken (ffmpeg crashed / disk full). Bail out and tell
                // the user — we don't sit forever in zombie-recording mode.
                video_recorder.stop();
                toast_text = "Recording stopped: pipe error";
                toast_time = std::chrono::steady_clock::now();
            }
        }

        // When the menu is open we need clicks to land on it, so we drop
        // WS_EX_TRANSPARENT temporarily. Restore click-through on close.
        bool menu_open_now = has_menu && menu.is_open();
        if (menu_open_now != menu_was_open) {
            LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            if (menu_open_now) {
                ex &= ~WS_EX_TRANSPARENT;
                SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
                SetForegroundWindow(hwnd);
                SetFocus(hwnd);
            } else {
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

    capture.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

} // namespace tubelight::overlay
