// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)
//
// Tubelight v0.1.0 — F2 entry point.
//
// Modes:
//   tubelight                       UI standalone (current: empty window)
//   tubelight --shader-only IMG     Loads IMG, applies the 8-pass pipeline, shows
//                                   the result in a window with keys 1..8 to toggle
//                                   individual passes and ESC to quit. (F2)
//   tubelight --help                Help.
//   tubelight --version             Version.
//
// Flags reserved for later phases (F3+): --target, --profile, --signal, --api,
// --fallback, --headless, --validate-profile, --screenshot, --export-slangp.

#include "core/gl_common.h"
#include "core/pipeline.h"
#include "core/texture.h"
#include "export/slangp_exporter.h"
#include "io/image_io.h"
#include "overlay/overlay_mode.h"
#include "profile/profile_loader.h"
#include "profile/validator.h"
#include "render/backend.h"

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include "capture/wgc_capture.h"
#include "render/backend_d3d12.h"
#include <d3d11.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <vector>
#include <string>
#include <string_view>

namespace {

// --bench: summarise per-frame GPU times (ms). Sorts in place.
void bench_report(const char* label, std::vector<double>& ms) {
    // stderr (unbuffered) — the GL path's stdout can be swallowed in some
    // headless/redirected environments; bench numbers must always show.
    if (ms.empty()) {
        std::fprintf(stderr, "[bench] %-4s: no GPU-timed samples (timestamp "
                     "queries unsupported on this backend?)\n", label);
        return;
    }
    std::sort(ms.begin(), ms.end());
    const size_t n = ms.size();
    double sum = 0.0;
    for (double v : ms) sum += v;
    const double avg = sum / static_cast<double>(n);
    const double p50 = ms[n / 2];
    const double p99 = ms[std::min(n - 1, static_cast<size_t>(n * 0.99))];
    const double fps = avg > 0.0 ? 1000.0 / avg : 0.0;
    std::fprintf(stderr, "[bench] %-4s | %zu frames | GPU/frame: avg %.3f ms "
                 "(%.1f fps) | p50 %.3f | p99 %.3f | min %.3f | max %.3f ms\n",
                 label, n, avg, fps, p50, p99, ms.front(), ms.back());
    std::fflush(stderr);
}

// Drive the backend through warmup + `frames` timed iterations, recording
// GPU time per frame (present/vsync independent — timestamp queries bracket
// only the command-list work). `render_one` issues the 8-pass pipeline.
template <class RenderFn>
int run_pipeline_bench(tubelight::IRenderBackend* be,
                       tubelight::Pipeline& pipeline,
                       RenderFn&& render_one,
                       int frames, const char* label) {
    if (!be) { std::fprintf(stderr, "[bench] no backend available\n"); return 1; }
    be->set_frame_timing(true);
    for (int i = 0; i < 30; ++i) {          // warmup: PSO/shader/driver settle
        pipeline.set_time(0.016f * static_cast<float>(i));
        be->begin_frame(); render_one(); be->end_frame(); be->finish();
    }
    std::vector<double> ms;
    ms.reserve(static_cast<size_t>(frames));
    for (int i = 0; i < frames; ++i) {
        pipeline.set_time(0.016f * static_cast<float>(30 + i));
        be->begin_frame(); render_one(); be->end_frame(); be->finish();
        const double g = be->last_frame_gpu_ms();
        if (g >= 0.0) ms.push_back(g);
    }
    be->set_frame_timing(false);
    bench_report(label, ms);
    return 0;
}

constexpr int kDefaultWidth  = 1280;
constexpr int kDefaultHeight = 960;
constexpr const char* kVersion = "0.2.0-rc.0";

// ---------------------------------------------------------------------------
// CLI argument parsing
// ---------------------------------------------------------------------------
struct Args {
    bool show_help    = false;
    bool show_version = false;
    std::string shader_only_input;
    std::string validate_profile_path;
    std::string profile_id;
    std::string signal_id;
    std::string export_slangp_path;
    bool overlay = false;
    bool overlay_fullscreen = false;
    int  overlay_monitor = 0;
    int  overlay_init_w = 1280;
    int  overlay_init_h = 960;
    std::string overlay_target_title;   // --overlay-target <title>
    int         overlay_target_pid = 0; // --overlay-target-pid <pid>
    bool overlay_region = false;
    int  region_x = 0, region_y = 0, region_w = 0, region_h = 0;
    bool unknown_flag = false;
    std::string unknown_flag_text;
    // Render backend selector (--renderer gl|dx12). Default to D3D12 on Windows
    // (zero-copy WGC capture + cross-process click-through); the D3D12 backend
    // falls back to GL automatically if device creation fails. GL elsewhere.
#if defined(TUBELIGHT_HAVE_D3D12)
    tubelight::BackendKind backend = tubelight::BackendKind::D3D12;
#else
    tubelight::BackendKind backend = tubelight::BackendKind::OpenGL;
#endif
    // Phase 3c F3c-5: deterministic offscreen capture for pixel-equivalence
    // testing. When set, --shader-only renders 60 warmup frames, captures
    // the backbuffer to <path> as PNG, then exits.
    std::string screenshot_path;
    // Phase 3d: standalone WGC capture test. Captures the primary monitor
    // via Windows.Graphics.Capture, feeds frames through the D3D12
    // pipeline, displays live in a window. Requires --renderer dx12
    // (the WGC→D3D11On12→D3D12 interop is the whole point).
    bool wgc_test = false;
    // Phase 3e: --bench <frames> times the 8-pass pipeline on the active
    // backend via GPU timestamp queries (present/vsync independent) over
    // the given frame count, prints avg/p50/p99/fps, exits. Works with
    // --shader-only on both --renderer gl and dx12 for a fair comparison.
    int bench_frames = 0;
};

Args parse_args(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            a.show_help = true;
        } else if (arg == "--version" || arg == "-v") {
            a.show_version = true;
        } else if (arg == "--shader-only") {
            if (i + 1 < argc) {
                a.shader_only_input = argv[++i];
            } else {
                a.unknown_flag = true;
                a.unknown_flag_text = "--shader-only requires a path";
            }
        } else if (arg == "--validate-profile") {
            if (i + 1 < argc) {
                a.validate_profile_path = argv[++i];
            } else {
                a.unknown_flag = true;
                a.unknown_flag_text = "--validate-profile requires a path";
            }
        } else if (arg == "--profile") {
            if (i + 1 < argc) a.profile_id = argv[++i];
        } else if (arg == "--signal") {
            if (i + 1 < argc) a.signal_id = argv[++i];
        } else if (arg == "--overlay") {
            a.overlay = true;
        } else if (arg == "--overlay-fullscreen") {
            a.overlay = true;
            a.overlay_fullscreen = true;
        } else if (arg == "--overlay-target") {
            if (i + 1 < argc) {
                a.overlay = true;
                a.overlay_target_title = argv[++i];
            } else {
                a.unknown_flag = true;
                a.unknown_flag_text = "--overlay-target requires a window title substring";
            }
        } else if (arg == "--overlay-target-pid") {
            if (i + 1 < argc) {
                a.overlay = true;
                a.overlay_target_pid = std::atoi(argv[++i]);
            } else {
                a.unknown_flag = true;
                a.unknown_flag_text = "--overlay-target-pid requires a numeric pid";
            }
        } else if (arg == "--overlay-region") {
            if (i + 1 < argc) {
                // x,y,w,h (comma separated, monitor-relative)
                std::string spec = argv[++i];
                int xv = 0, yv = 0, wv = 0, hv = 0;
                if (std::sscanf(spec.c_str(), "%d,%d,%d,%d", &xv, &yv, &wv, &hv) == 4
                    && wv > 0 && hv > 0) {
                    a.overlay = true;
                    a.overlay_region = true;
                    a.region_x = xv; a.region_y = yv;
                    a.region_w = wv; a.region_h = hv;
                } else {
                    a.unknown_flag = true;
                    a.unknown_flag_text = "--overlay-region needs x,y,w,h (e.g. 100,100,800,600)";
                }
            } else {
                a.unknown_flag = true;
                a.unknown_flag_text = "--overlay-region requires x,y,w,h";
            }
        } else if (arg == "--monitor") {
            if (i + 1 < argc) a.overlay_monitor = std::atoi(argv[++i]);
        } else if (arg == "--size") {
            if (i + 2 < argc) {
                a.overlay_init_w = std::atoi(argv[++i]);
                a.overlay_init_h = std::atoi(argv[++i]);
            }
        } else if (arg == "--wgc-test") {
            a.wgc_test = true;
            a.backend  = tubelight::BackendKind::D3D12;  // implied
        } else if (arg == "--screenshot") {
            if (i + 1 < argc) {
                a.screenshot_path = argv[++i];
            } else {
                a.unknown_flag = true;
                a.unknown_flag_text = "--screenshot requires a path";
            }
        } else if (arg == "--bench") {
            if (i + 1 < argc) {
                a.bench_frames = std::atoi(argv[++i]);
                if (a.bench_frames <= 0) {
                    a.unknown_flag = true;
                    a.unknown_flag_text = "--bench requires a positive frame count";
                }
            } else {
                a.unknown_flag = true;
                a.unknown_flag_text = "--bench requires a frame count";
            }
        } else if (arg == "--renderer") {
            if (i + 1 < argc) {
                const char* tok = argv[++i];
                tubelight::BackendKind k;
                if (tubelight::parse_backend_kind(tok, k)) {
                    a.backend = k;
                } else {
                    a.unknown_flag = true;
                    a.unknown_flag_text = std::string("--renderer ") + tok +
                                          " (valid: 'gl' or 'dx12')";
                }
            } else {
                a.unknown_flag = true;
                a.unknown_flag_text = "--renderer requires a value (gl)";
            }
        } else if (arg == "--export-slangp") {
            if (i + 1 < argc) {
                a.export_slangp_path = argv[++i];
            } else {
                a.unknown_flag = true;
                a.unknown_flag_text = "--export-slangp requires a path";
            }
        } else if (arg.substr(0, 2) == "--") {
            // F3+ flags not implemented yet; treat as no-op for now so smoke
            // tests like --target X --profile Y don't crash F2.
        } else {
            a.unknown_flag = true;
            a.unknown_flag_text = std::string(arg);
        }
    }
    return a;
}

void print_help() {
    std::printf(
        "Tubelight %s — high-fidelity CRT overlay\n"
        "\n"
        "Usage:\n"
        "  tubelight [options]\n"
        "\n"
        "Options:\n"
        "  --help, -h                   Print this help and exit\n"
        "  --version, -v                Print version and exit\n"
        "  --overlay                    Real overlay (Windows): a resizable Win32\n"
        "                               window whose content is the area underneath,\n"
        "                               processed through the 8-pass CRT pipeline live.\n"
        "  --overlay-fullscreen         Same but borderless fullscreen topmost.\n"
        "  --overlay-target <title>     Track a specific window by title substring.\n"
        "                               Tubelight follows its position+size every frame;\n"
        "                               click-through so the underlying app stays usable.\n"
        "  --overlay-target-pid <pid>   Same but match by process id (more robust).\n"
        "  --overlay-region x,y,w,h     Pin the overlay to a fixed monitor-relative\n"
        "                               rectangle (no window tracking). Click-through.\n"
        "  --monitor <index>            Which display to capture from (default 0)\n"
        "  --size <w> <h>               Initial window size for windowed mode (1280x960)\n"
        "  --shader-only <path>         Apply pipeline to a PNG and show in a window\n"
        "  --target <pid|exe>           [F5+] Attach overlay to a process\n"
        "  --profile <id>               CRT profile id\n"
        "  --signal <id>                Signal profile id\n"
        "  --validate-profile <path>    Validate a profile JSON and exit\n"
        "  --export-slangp <path>       Export current profile as RetroArch preset\n"
        "  --wgc-test                   [Phase 3d] Capture the primary monitor via\n"
        "                               Windows.Graphics.Capture and feed frames through\n"
        "                               the D3D12 pipeline live (implies --renderer dx12).\n"
        "                               Standalone smoke for WGC + D3D11On12 interop. ESC\n"
        "                               quits. Combine with --screenshot to capture frame\n"
        "                               60 and exit.\n"
        "  --screenshot <path>          --shader-only deterministic capture: renders 60\n"
        "                               warmup frames, writes the backbuffer to <path>\n"
        "                               as PNG, exits. Used by tests/golden pixel-\n"
        "                               equivalence harness (GL vs DX12 PSNR).\n"
        "  --renderer <gl|dx12>         Render backend (default: gl).\n"
        "                               'dx12' drives the full 8-pass CRT pipeline on\n"
        "                               Direct3D 12 and, combined with --overlay*, captures\n"
        "                               via Windows.Graphics.Capture (WGC) instead of DXGI\n"
        "                               Desktop Duplication. Falls back to gl if the D3D12\n"
        "                               device cannot be created.\n"
        "\n"
        "Note: capture + render backends pair up. gl  → DXGI Desktop\n"
        "      Duplication + OpenGL; dx12 → WGC + D3D11On12 + Direct3D 12.\n"
        "      Either way the underlying apps (games / emulators) may use\n"
        "      DirectX, OpenGL or Vulkan — all captured transparently.\n"
        "      DX12 overlay: ImGui menu + cross-process click-through are\n"
        "      deferred to v0.2.1 (Phase 4a / DirectComposition).\n"
        "\n"
        "Overlay global hotkeys (Ctrl+Alt+ ...):\n"
        "  Q  quit            M  toggle menu      F  freeze frame\n"
        "  Enter  toggle fullscreen (preserves aspect)\n"
        "  T  attach/detach foreground window     H  toggle status HUD\n"
        "  C  toggle click-through (windowed)     S  PNG screenshot\n"
        "  V  toggle MP4 recording (needs ffmpeg in PATH)\n"
        "  R  toggle recordable (Snipping Tool / Game Bar / OBS can see overlay)\n"
        "\n"
        "Developer / debug hotkeys (only useful if you want to see what\n"
        "each shader pass contributes — most users can ignore these):\n"
        "  0          re-enable every pass (use after toggling some off)\n"
        "  1..8       toggle individual passes -1..6 of the CRT pipeline\n"
        "             (signal, beam, mask, bloom, temporal, composition...)\n"
        "\n"
        "Interactive keys (in --shader-only mode):\n"
        "  ESC  Quit  |  1..8 toggle pass −1..6  |  0 enable all  |  R reload shaders\n"
        "\n"
        "See docs/USER_GUIDE.md + docs/BEZELS.md for details.\n",
        kVersion
    );
}

void print_version() {
#if defined(TUBELIGHT_HAVE_D3D12)
    std::printf("tubelight %s (renderers: gl, dx12)\n", kVersion);
#else
    std::printf("tubelight %s (renderers: gl)\n", kVersion);
#endif
}

// ---------------------------------------------------------------------------
// GLFW lifecycle
// ---------------------------------------------------------------------------
void glfw_error_callback(int code, const char* description) {
    std::fprintf(stderr, "[tubelight][glfw error %d] %s\n", code, description);
}

struct AppState {
    tubelight::Pipeline* pipeline = nullptr;
};

void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    if (action != GLFW_PRESS) return;

    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));

    if (key == GLFW_KEY_ESCAPE) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        return;
    }

    if (!state || !state->pipeline) return;

    // 1..8 → toggle passes 0..7 (which represent Pass −1 .. Pass 6)
    if (key >= GLFW_KEY_1 && key <= GLFW_KEY_8) {
        int idx = key - GLFW_KEY_1;
        bool currently = state->pipeline->is_pass_enabled(idx);
        state->pipeline->set_pass_enabled(idx, !currently);
        std::printf("[tubelight] %s: %s\n",
                    tubelight::pass_display_name(idx),
                    !currently ? "ON" : "OFF");
    } else if (key == GLFW_KEY_0) {
        for (int i = 0; i < tubelight::Pipeline::kPassCount; ++i) {
            state->pipeline->set_pass_enabled(i, true);
        }
        std::printf("[tubelight] all passes ON\n");
    }
}

void framebuffer_resize_callback(GLFWwindow* window, int width, int height) {
    auto* state = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (state && state->pipeline) {
        state->pipeline->resize(width, height);
    }
    glViewport(0, 0, width, height);
}

// ---------------------------------------------------------------------------
// Application entry: open window + pipeline + main loop
// ---------------------------------------------------------------------------
int run_shader_only(const std::string& image_path,
                    const std::string& profile_id,
                    const std::string& signal_id,
                    const std::string& screenshot_path,
                    int bench_frames = 0) {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::fprintf(stderr, "[tubelight] glfwInit failed\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    const std::string title = std::string("Tubelight ") + kVersion + " — shader-only (ESC to quit, 1..8 toggle passes)";
    GLFWwindow* window = glfwCreateWindow(kDefaultWidth, kDefaultHeight, title.c_str(), nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "[tubelight] glfwCreateWindow failed (GL 4.5 core not available?)\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    // Load source texture
    tubelight::Texture2D source_tex;
    if (!source_tex.load_from_file(image_path)) {
        std::fprintf(stderr, "[tubelight] cannot load %s: %s\n",
                     image_path.c_str(), source_tex.get_error().c_str());
        glfwDestroyWindow(window);
        glfwTerminate();
        return 2;
    }

    // Build pipeline at window size
    int fb_w = 0, fb_h = 0;
    glfwGetFramebufferSize(window, &fb_w, &fb_h);
    tubelight::Pipeline pipeline;
    if (!pipeline.create(fb_w, fb_h)) {
        std::fprintf(stderr, "[tubelight] pipeline.create failed\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    AppState state{&pipeline};
    glfwSetWindowUserPointer(window, &state);
    glfwSetKeyCallback(window, key_callback);
    glfwSetFramebufferSizeCallback(window, framebuffer_resize_callback);

    // Apply CRT profile if requested.
    if (!profile_id.empty()) {
        std::string err;
        auto p = tubelight::load_crt_profile_by_id(profile_id, err);
        if (!p.has_value()) {
            std::fprintf(stderr, "[tubelight] CRT profile '%s' not loaded: %s\n",
                         profile_id.c_str(), err.c_str());
        } else {
            pipeline.apply_crt_profile(p.value());
            std::printf("[tubelight] CRT profile: %s\n", p->display_name.c_str());
        }
    }

    // Apply signal profile if requested (defaults to pristine RGB if not).
    if (!signal_id.empty()) {
        std::string err;
        auto s = tubelight::load_signal_profile_by_id(signal_id, err);
        if (!s.has_value()) {
            std::fprintf(stderr, "[tubelight] signal profile '%s' not loaded: %s\n",
                         signal_id.c_str(), err.c_str());
        } else {
            pipeline.apply_signal_profile(s.value());
            std::printf("[tubelight] signal profile: %s\n", s->display_name.c_str());
        }
    }

    // --bench: GPU-timed throughput of the 8-pass pipeline, then exit.
    if (bench_frames > 0) {
        const uint32_t src = source_tex.id();
        int rc = run_pipeline_bench(pipeline.backend(), pipeline,
                                    [&] { pipeline.render_to_screen(src); },
                                    bench_frames, "gl");
        glfwDestroyWindow(window);
        glfwTerminate();
        return rc;
    }

    std::printf("[tubelight] shader-only running on %s (%dx%d).\n",
                image_path.c_str(), source_tex.width(), source_tex.height());
    std::printf("[tubelight] Keys: 1..8 toggle passes, 0 enable all, ESC quit.\n");

    // Screenshot mode: render 60 frames with a fixed time stamp (so the
    // signal noise pass is deterministic), then glReadPixels the backbuffer
    // and write a PNG. Exits without entering the interactive loop.
    if (!screenshot_path.empty()) {
        constexpr int kWarmupFrames = 60;
        const float kFixedTime = 1.0f;
        pipeline.set_time(kFixedTime);
        for (int f = 0; f < kWarmupFrames; ++f) {
            pipeline.render_to_screen(source_tex.id());
            glfwSwapBuffers(window);
            glfwPollEvents();
        }
        // Capture the frontbuffer that we just presented. glReadPixels
        // from GL_FRONT after SwapBuffers gives the visible image.
        std::vector<uint8_t> px(static_cast<size_t>(fb_w) * fb_h * 4);
        glReadBuffer(GL_FRONT);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, fb_w, fb_h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        std::string err;
        const bool ok = tubelight::save_png(screenshot_path, px.data(),
                                             fb_w, fb_h, 4, err,
                                             /*flip_vertical=*/true);
        if (!ok) {
            std::fprintf(stderr, "[tubelight] screenshot save failed: %s\n", err.c_str());
        } else {
            std::printf("[tubelight] screenshot written: %s (%dx%d)\n",
                        screenshot_path.c_str(), fb_w, fb_h);
        }
        glfwDestroyWindow(window);
        glfwTerminate();
        return ok ? 0 : 1;
    }

    double t0 = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        pipeline.set_time(static_cast<float>(glfwGetTime() - t0));
        pipeline.render_to_screen(source_tex.id());
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

// Phase 3c F3c-4: open a NO_API window with a Win32 HWND, drive the full
// 8-pass Pipeline on a D3D12Backend. Loads the testcard via stb_image,
// uploads it into a backend-owned TextureHandle, then per-frame:
//   backend->begin_frame() → pipeline.render_to_screen(handle) → backend->end_frame()
// Returns 1 if any step fails — caller may fall back to GL.
int run_shader_only_dx12(const std::string& image_path,
                          const std::string& profile_id,
                          const std::string& signal_id,
                          const std::string& screenshot_path,
                          int bench_frames = 0) {
#if defined(_WIN32) && defined(TUBELIGHT_HAVE_D3D12)
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::fprintf(stderr, "[tubelight] glfwInit failed\n");
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE,  GLFW_TRUE);

    const std::string title = std::string("Tubelight ") + kVersion +
                              " — shader-only D3D12 (ESC to quit)";
    GLFWwindow* window = glfwCreateWindow(kDefaultWidth, kDefaultHeight,
                                          title.c_str(), nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "[tubelight] glfwCreateWindow (NO_API) failed\n");
        glfwTerminate();
        return 1;
    }

    HWND hwnd = glfwGetWin32Window(window);
    int fb_w = 0, fb_h = 0;
    glfwGetFramebufferSize(window, &fb_w, &fb_h);

    auto backend = tubelight::create_backend(tubelight::BackendKind::D3D12);
    if (!backend) {
        std::fprintf(stderr, "[tubelight] D3D12 backend not compiled in\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    tubelight::BackendInitParams bp;
    bp.native_window_handle = hwnd;
    bp.width  = fb_w;
    bp.height = fb_h;
    bp.enable_debug = false;  // re-enable when iterating on render quality
    if (!backend->init(bp)) {
        std::fprintf(stderr,
            "[tubelight] D3D12Backend::init failed — fallback to GL backend will be tried.\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    std::printf("[tubelight] %s\n", backend->name());

    // Load testcard via stb_image into a CPU RGBA8 buffer, then upload
    // into a backend-owned TextureHandle. flip_vertical=false: DX12
    // backend uses a Vulkan-style negative-height viewport so the same
    // SPIR-V semantics apply (uv.y=0 → top of viewport). DX12 texture
    // convention: texel y=0 = top of texture. With flip=false (PNG
    // natural order), top of texture = top of PNG = top of screen. ✓
    tubelight::ImageData img;
    std::string img_err;
    if (!tubelight::load_image(image_path, img, img_err, /*flip_vertical=*/false)) {
        std::fprintf(stderr, "[tubelight] cannot load %s: %s\n",
                     image_path.c_str(), img_err.c_str());
        backend->shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 2;
    }
    if (img.channels != 4) {
        std::fprintf(stderr, "[tubelight] testcard must be RGBA8 (got %d channels)\n",
                     img.channels);
        backend->shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 2;
    }
    const int img_w = img.width;
    const int img_h = img.height;
    const uint8_t* pixels = img.pixels.data();

    // Inject backend into Pipeline BEFORE create() so Pipeline doesn't
    // instantiate a default GL backend.
    tubelight::Pipeline pipeline;
    auto* backend_raw = backend.get();
    pipeline.set_backend(std::move(backend));
    if (!pipeline.create(fb_w, fb_h)) {
        std::fprintf(stderr, "[tubelight] pipeline.create failed on D3D12\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    tubelight::TextureDesc td;
    td.width  = img_w;
    td.height = img_h;
    td.format = tubelight::PixelFormat::RGBA8_UNORM;
    tubelight::TextureHandle source_h = backend_raw->create_texture(td);
    if (!source_h.is_valid()) {
        std::fprintf(stderr, "[tubelight] D3D12 source texture create failed\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    if (!backend_raw->upload_texture_rgba8(source_h, pixels, img_w, img_h)) {
        std::fprintf(stderr, "[tubelight] D3D12 source texture upload failed\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    // img owns the pixel buffer via std::vector; nothing to free.

    if (!profile_id.empty()) {
        std::string err;
        auto p = tubelight::load_crt_profile_by_id(profile_id, err);
        if (p.has_value()) {
            pipeline.apply_crt_profile(p.value());
            std::printf("[tubelight] CRT profile: %s\n", p->display_name.c_str());
        } else {
            std::fprintf(stderr, "[tubelight] CRT profile '%s' not loaded: %s\n",
                         profile_id.c_str(), err.c_str());
        }
    }
    if (!signal_id.empty()) {
        std::string err;
        auto s = tubelight::load_signal_profile_by_id(signal_id, err);
        if (s.has_value()) {
            pipeline.apply_signal_profile(s.value());
            std::printf("[tubelight] signal profile: %s\n", s->display_name.c_str());
        } else {
            std::fprintf(stderr, "[tubelight] signal profile '%s' not loaded: %s\n",
                         signal_id.c_str(), err.c_str());
        }
    }

    // --bench: GPU-timed throughput of the 8-pass pipeline, then exit.
    if (bench_frames > 0) {
        int rc = run_pipeline_bench(backend_raw, pipeline,
                                    [&] { pipeline.render_to_screen(source_h); },
                                    bench_frames, "dx12");
        glfwDestroyWindow(window);
        glfwTerminate();
        return rc;
    }

    struct DXResize {
        tubelight::IRenderBackend* be;
        tubelight::Pipeline* pl;
    } ctx{ backend_raw, &pipeline };
    glfwSetWindowUserPointer(window, &ctx);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int ww, int hh) {
        auto* c = static_cast<DXResize*>(glfwGetWindowUserPointer(w));
        if (!c) return;
        if (c->be) c->be->resize(ww, hh);
        if (c->pl) c->pl->resize(ww, hh);
    });
    glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int, int act, int) {
        if (act == GLFW_PRESS && key == GLFW_KEY_ESCAPE) {
            glfwSetWindowShouldClose(w, GLFW_TRUE);
        }
    });

    std::printf("[tubelight] D3D12 shader-only running on %s (%dx%d).\n",
                image_path.c_str(), img_w, img_h);

    // Screenshot mode: render 60 warmup frames with fixed time, capture
    // backbuffer via readback, save PNG, exit.
    if (!screenshot_path.empty()) {
        constexpr int kWarmupFrames = 60;
        const float kFixedTime = 1.0f;
        pipeline.set_time(kFixedTime);
        for (int f = 0; f < kWarmupFrames; ++f) {
            backend_raw->begin_frame();
            pipeline.render_to_screen(source_h);
            backend_raw->end_frame();
            glfwPollEvents();
        }
        std::vector<uint8_t> px;
        int cap_w = 0, cap_h = 0;
        if (!backend_raw->capture_backbuffer(px, cap_w, cap_h)) {
            std::fprintf(stderr, "[tubelight] D3D12 capture_backbuffer failed\n");
            glfwDestroyWindow(window);
            glfwTerminate();
            return 1;
        }
        std::string err;
        // DX12 backbuffer is BGRA-ish? Actually R8G8B8A8_UNORM is RGBA.
        // save_png expects RGBA. No swizzle needed.
        const bool ok = tubelight::save_png(screenshot_path, px.data(),
                                             cap_w, cap_h, 4, err,
                                             /*flip_vertical=*/false);
        if (!ok) {
            std::fprintf(stderr, "[tubelight] screenshot save failed: %s\n", err.c_str());
        } else {
            std::printf("[tubelight] screenshot written: %s (%dx%d)\n",
                        screenshot_path.c_str(), cap_w, cap_h);
        }
        glfwDestroyWindow(window);
        glfwTerminate();
        return ok ? 0 : 1;
    }

    double t0 = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        pipeline.set_time(static_cast<float>(glfwGetTime() - t0));
        backend_raw->begin_frame();
        pipeline.render_to_screen(source_h);
        backend_raw->end_frame();
        glfwPollEvents();
        glfwGetFramebufferSize(window, &fb_w, &fb_h);
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
#else
    (void)image_path; (void)profile_id; (void)signal_id;
    std::fprintf(stderr, "[tubelight] D3D12 backend not available on this build/platform.\n");
    return 1;
#endif
}

// Phase 3d standalone smoke: WGC captures the primary monitor + feeds
// frames through the D3D12 pipeline + displays live in a GLFW window.
// No overlay integration, no DXGI Duplication, no GL. Exits on ESC or
// (if --screenshot is set) after 60 warmup frames.
int run_wgc_test(const std::string& profile_id,
                 const std::string& signal_id,
                 const std::string& screenshot_path) {
#if defined(_WIN32) && defined(TUBELIGHT_HAVE_D3D12)
    if (!tubelight::WgcCapture::is_supported()) {
        std::fprintf(stderr, "[tubelight] WGC unsupported on this Windows build (requires 1903+)\n");
        return 1;
    }
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::fprintf(stderr, "[tubelight] glfwInit failed\n");
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE,  GLFW_TRUE);
    const std::string title = std::string("Tubelight ") + kVersion +
                              " — WGC monitor capture (DX12, ESC to quit)";
    GLFWwindow* window = glfwCreateWindow(kDefaultWidth, kDefaultHeight,
                                          title.c_str(), nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    HWND hwnd = glfwGetWin32Window(window);
    int fb_w = 0, fb_h = 0;
    glfwGetFramebufferSize(window, &fb_w, &fb_h);

    auto backend = tubelight::create_backend(tubelight::BackendKind::D3D12);
    if (!backend) { glfwDestroyWindow(window); glfwTerminate(); return 1; }
    tubelight::BackendInitParams bp;
    bp.native_window_handle = hwnd;
    bp.width  = fb_w;
    bp.height = fb_h;
    bp.enable_debug = false;
    if (!backend->init(bp)) {
        std::fprintf(stderr, "[tubelight] D3D12Backend::init failed for WGC test\n");
        glfwDestroyWindow(window); glfwTerminate(); return 1;
    }
    auto* d12 = static_cast<tubelight::D3D12Backend*>(backend.get());

    // Wire the D3D11On12 device to WGC.
    ID3D11Device* d3d11 = d12->d3d11_on12_device();
    if (!d3d11) {
        std::fprintf(stderr, "[tubelight] D3D11On12 device init failed\n");
        backend->shutdown(); glfwDestroyWindow(window); glfwTerminate(); return 1;
    }

    tubelight::WgcCapture wgc;
    // Primary monitor for the smoke. Tubelight's own window may show up
    // on the captured monitor — that's intentional (you can watch the
    // effect "pipeline-in-pipeline"). A future flag could pick by HMONITOR
    // index or by target HWND.
    HMONITOR mon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    if (!wgc.init_for_monitor(mon, d3d11)) {
        std::fprintf(stderr, "[tubelight] WgcCapture::init_for_monitor failed\n");
        backend->shutdown(); glfwDestroyWindow(window); glfwTerminate(); return 1;
    }
    if (!wgc.start()) {
        std::fprintf(stderr, "[tubelight] WgcCapture::start failed\n");
        backend->shutdown(); glfwDestroyWindow(window); glfwTerminate(); return 1;
    }
    std::printf("[tubelight] WGC test running — capturing primary monitor.\n");

    tubelight::Pipeline pipeline;
    auto* backend_raw = backend.get();
    pipeline.set_backend(std::move(backend));
    if (!pipeline.create(fb_w, fb_h)) {
        std::fprintf(stderr, "[tubelight] pipeline.create failed for WGC test\n");
        wgc.stop(); glfwDestroyWindow(window); glfwTerminate(); return 1;
    }
    if (!profile_id.empty()) {
        std::string err;
        auto p = tubelight::load_crt_profile_by_id(profile_id, err);
        if (p) pipeline.apply_crt_profile(*p);
    }
    if (!signal_id.empty()) {
        std::string err;
        auto s = tubelight::load_signal_profile_by_id(signal_id, err);
        if (s) pipeline.apply_signal_profile(*s);
    }

    glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int, int act, int) {
        if (act == GLFW_PRESS && key == GLFW_KEY_ESCAPE) {
            glfwSetWindowShouldClose(w, GLFW_TRUE);
        }
    });

    double t0 = glfwGetTime();
    int frames_rendered = 0;
    const int kMaxScreenshotFrames = 60;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        int tw = 0, th = 0;
        auto tex11 = wgc.latest_frame(tw, th);
        if (!tex11) {
            // No frame yet (first ~1-2 ticks). Sleep a hair and retry.
            Sleep(2);
            continue;
        }
        auto h = static_cast<tubelight::D3D12Backend*>(backend_raw)
                     ->wrap_d3d11_texture(tex11.Get(), tw, th);
        if (!h.is_valid()) {
            std::fprintf(stderr, "[tubelight] wrap_d3d11_texture failed\n");
            break;
        }
        pipeline.set_time(static_cast<float>(glfwGetTime() - t0));
        backend_raw->begin_frame();
        pipeline.render_to_screen(h);
        backend_raw->end_frame();
        ++frames_rendered;

        if (!screenshot_path.empty() && frames_rendered >= kMaxScreenshotFrames) {
            std::vector<uint8_t> px;
            int cw = 0, ch = 0;
            if (backend_raw->capture_backbuffer(px, cw, ch)) {
                std::string err;
                tubelight::save_png(screenshot_path, px.data(), cw, ch, 4, err, false);
                std::printf("[tubelight] WGC test screenshot: %s (%dx%d, %llu frames captured)\n",
                            screenshot_path.c_str(), cw, ch,
                            static_cast<unsigned long long>(wgc.frame_count()));
            }
            break;
        }
    }
    wgc.stop();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
#else
    (void)profile_id; (void)signal_id; (void)screenshot_path;
    std::fprintf(stderr, "[tubelight] WGC test requires Windows + D3D12 backend\n");
    return 1;
#endif
}

int run_empty_window() {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::fprintf(stderr, "[tubelight] glfwInit failed\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    const std::string title = std::string("Tubelight ") + kVersion + " — F2 skeleton (ESC to quit)";
    GLFWwindow* window = glfwCreateWindow(800, 600, title.c_str(), nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "[tubelight] glfwCreateWindow failed (GL 4.5 core not available?)\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    std::printf("[tubelight] empty window (no target/profile set). ESC to quit.\n");

    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.05f, 0.05f, 0.05f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glfwSwapBuffers(window);
        glfwPollEvents();
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    Args args = parse_args(argc, argv);

    if (args.show_help) {
        print_help();
        return 0;
    }
    if (args.show_version) {
        print_version();
        return 0;
    }
    if (args.unknown_flag) {
        std::fprintf(stderr, "[tubelight] unknown flag or missing argument: %s\n",
                     args.unknown_flag_text.c_str());
        std::fprintf(stderr, "Run --help for usage.\n");
        return 1;
    }

    if (!args.validate_profile_path.empty()) {
        auto r = tubelight::validate_profile_file(args.validate_profile_path);
        return tubelight::print_validation_result(args.validate_profile_path, r);
    }
    if (!args.export_slangp_path.empty()) {
        if (args.profile_id.empty() || args.signal_id.empty()) {
            std::fprintf(stderr,
                "--export-slangp requires both --profile <id> and --signal <id>.\n");
            return 1;
        }
        std::string err;
        auto crt = tubelight::load_crt_profile_by_id(args.profile_id, err);
        if (!crt) {
            std::fprintf(stderr, "CRT profile '%s' not found: %s\n",
                         args.profile_id.c_str(), err.c_str());
            return 2;
        }
        auto sig = tubelight::load_signal_profile_by_id(args.signal_id, err);
        if (!sig) {
            std::fprintf(stderr, "Signal profile '%s' not found: %s\n",
                         args.signal_id.c_str(), err.c_str());
            return 2;
        }
        if (!tubelight::exporter::export_slangp(crt.value(), sig.value(),
                                                 args.export_slangp_path, err)) {
            std::fprintf(stderr, "Export failed: %s\n", err.c_str());
            return 1;
        }
        std::printf("[tubelight] exported .slangp preset to %s\n",
                    args.export_slangp_path.c_str());
        return 0;
    }
    if (args.wgc_test) {
        return run_wgc_test(args.profile_id, args.signal_id, args.screenshot_path);
    }
    if (!args.shader_only_input.empty()) {
        if (args.backend == tubelight::BackendKind::D3D12) {
            int rc = run_shader_only_dx12(args.shader_only_input,
                                            args.profile_id, args.signal_id,
                                            args.screenshot_path,
                                            args.bench_frames);
            if (rc == 0) return 0;
            std::fprintf(stderr,
                "[tubelight] D3D12 path failed (rc=%d); falling back to OpenGL.\n", rc);
            // Fall through to GL.
        }
        return run_shader_only(args.shader_only_input, args.profile_id,
                               args.signal_id, args.screenshot_path,
                               args.bench_frames);
    }
    // Default action (no flags, or just --overlay / --overlay-fullscreen):
    // launch the real overlay. Double-clicking the exe should "just work".
    tubelight::overlay::Options o;
    // Out-of-box default: the clean "basic" preset (aperture-grille grid + soft
    // scanlines, no other effects, RGB signal — no NTSC artifacts). The user
    // can change it and "Guardar como predeterminada" later (Phase 3).
    o.profile_id    = args.profile_id.empty() ? std::string("basic")   : args.profile_id;
    o.signal_id     = args.signal_id.empty()  ? std::string("rgb_vga") : args.signal_id;
    // No explicit profile/signal on the CLI → let the overlay load the user's
    // saved default config (if any) over the built-in basic preset.
    o.use_saved_default = args.profile_id.empty() && args.signal_id.empty();
    o.monitor_index = args.overlay_monitor;
    const bool has_target = !args.overlay_target_title.empty() || args.overlay_target_pid > 0;
    if (has_target) {
        o.mode = tubelight::overlay::OverlayMode::TargetWindow;
    } else if (args.overlay_region) {
        o.mode = tubelight::overlay::OverlayMode::Region;
    } else if (args.overlay_fullscreen) {
        o.mode = tubelight::overlay::OverlayMode::Fullscreen;
    } else {
        o.mode = tubelight::overlay::OverlayMode::Windowed;
    }
    o.init_w = args.overlay_init_w;
    o.init_h = args.overlay_init_h;
    o.target_window = args.overlay_target_title;
    o.target_pid    = args.overlay_target_pid;
    o.region_x = args.region_x;
    o.region_y = args.region_y;
    o.region_w = args.region_w;
    o.region_h = args.region_h;
    // T5.5: forward --renderer dx12 into the overlay so it takes the
    // WGC + D3D11On12 + D3D12 path (run_dx12). Defaults to GL otherwise.
    o.backend = args.backend;
    o.bench_frames = args.bench_frames;  // Phase 3e end-to-end capture bench
    return tubelight::overlay::run(o);
}
