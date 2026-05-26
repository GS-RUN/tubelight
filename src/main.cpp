// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// Tubelight v0.1.0 — F1 skeleton.
// Opens a GLFW window and closes on ESC. The full 8-pass CRT pipeline
// is introduced in F2 (see specs/PLAN.LOCKED.md).

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace {

constexpr int kDefaultWidth  = 800;
constexpr int kDefaultHeight = 600;
constexpr const char* kVersion = "0.1.0-alpha";

void glfw_error_callback(int code, const char* description) {
    std::fprintf(stderr, "[tubelight][glfw error %d] %s\n", code, description);
}

void key_callback(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

void print_help() {
    std::printf(
        "Tubelight %s — high-fidelity CRT overlay\n"
        "\n"
        "Usage:\n"
        "  tubelight [options]\n"
        "\n"
        "Options:\n"
        "  --help, -h           Print this help and exit\n"
        "  --version, -v        Print version and exit\n"
        "  --target <pid|exe>   Attach overlay to a process (F5+, not active yet)\n"
        "  --profile <id>       CRT profile id (F3+, not active yet)\n"
        "  --signal <id>        Signal profile id (F3+, not active yet)\n"
        "  --api <auto|dx11|dx12|opengl|vulkan>   API hint for injection (F5+)\n"
        "  --fallback <auto|always>               Force DXGI/PipeWire fallback (F5+)\n"
        "  --headless           Run without UI (F3+)\n"
        "  --screenshot <path>  Save a screenshot of the next frame and exit (F2+)\n"
        "  --export-slangp <path>   Export current profile as RetroArch preset (F7)\n"
        "  --validate-profile <path>   Validate a profile JSON and exit (F3+)\n"
        "  --shader-only <path>        Apply pipeline to a PNG/MP4 in a preview window (F2+)\n"
        "\n"
        "F1 (current): window opens and closes on ESC; no pipeline yet.\n"
        "See docs/USER_GUIDE.md for build instructions, specs/PLAN.LOCKED.md for roadmap.\n",
        kVersion
    );
}

void print_version() {
    std::printf("tubelight %s\n", kVersion);
}

// Returns exit code if a "terminating" flag was handled (e.g. --help), -1 otherwise.
int handle_terminating_flags(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_help();
            return 0;
        }
        if (arg == "--version" || arg == "-v") {
            print_version();
            return 0;
        }
    }
    return -1;
}

} // namespace

int main(int argc, char** argv) {
    if (int rc = handle_terminating_flags(argc, argv); rc != -1) {
        return rc;
    }

    glfwSetErrorCallback(glfw_error_callback);

    if (!glfwInit()) {
        std::fprintf(stderr, "[tubelight] glfwInit failed\n");
        return 1;
    }

    // OpenGL 4.5 core. The full pipeline (F2+) will need this; F1 only opens the window.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

    const std::string title = std::string("Tubelight ") + kVersion + " — F1 skeleton (ESC to quit)";

    GLFWwindow* window = glfwCreateWindow(
        kDefaultWidth, kDefaultHeight,
        title.c_str(),
        nullptr, nullptr
    );

    if (!window) {
        std::fprintf(stderr, "[tubelight] glfwCreateWindow failed (no GL 4.5 core?)\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSetKeyCallback(window, key_callback);
    glfwSwapInterval(1); // vsync on

    std::printf("[tubelight] window opened (%dx%d). Press ESC to quit.\n",
                kDefaultWidth, kDefaultHeight);

    while (!glfwWindowShouldClose(window)) {
        // F1: no rendering yet. F2 introduces the pipeline.
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    std::printf("[tubelight] shutting down.\n");
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
