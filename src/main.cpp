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
#include "overlay/overlay_mode.h"
#include "profile/profile_loader.h"
#include "profile/validator.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace {

constexpr int kDefaultWidth  = 1280;
constexpr int kDefaultHeight = 960;
constexpr const char* kVersion = "0.1.2";

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
        "\n"
        "Note: Tubelight renders with OpenGL. Desktop capture uses Windows'\n"
        "      DXGI Desktop Duplication regardless of what the underlying\n"
        "      apps (games / emulators) use — DirectX, OpenGL, Vulkan are\n"
        "      all captured transparently. There is no rendering-API switch.\n"
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
    std::printf("tubelight %s\n", kVersion);
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
                    const std::string& signal_id) {
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

    std::printf("[tubelight] shader-only running on %s (%dx%d).\n",
                image_path.c_str(), source_tex.width(), source_tex.height());
    std::printf("[tubelight] Keys: 1..8 toggle passes, 0 enable all, ESC quit.\n");

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
    if (!args.shader_only_input.empty()) {
        return run_shader_only(args.shader_only_input, args.profile_id, args.signal_id);
    }
    // Default action (no flags, or just --overlay / --overlay-fullscreen):
    // launch the real overlay. Double-clicking the exe should "just work".
    tubelight::overlay::Options o;
    o.profile_id    = args.profile_id.empty() ? std::string("pvm-8220")     : args.profile_id;
    o.signal_id     = args.signal_id.empty()  ? std::string("composite_ntsc"): args.signal_id;
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
    return tubelight::overlay::run(o);
}
