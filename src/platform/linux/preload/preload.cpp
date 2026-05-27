// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)
//
// libtubelight_preload.so — LD_PRELOAD shim for Linux.
//
// Usage:
//   LD_PRELOAD=/path/to/libtubelight_preload.so retroarch
//
// What it does today (F5):
//   - Intercepts glXSwapBuffers (X11) and eglSwapBuffers (EGL/Wayland).
//   - On the first call: logs the hook is active.
//   - Forwards every call to the real implementation via dlsym(RTLD_NEXT, ...).
//   - Tracks frame count for the lifetime of the process.
//
// What it will do in F7:
//   - Inside the hook, capture the current back buffer with glReadPixels
//     OR (preferred) re-render through Tubelight's 8-pass pipeline into the
//     same default framebuffer before the swap completes — adding <2 ms.
//   - Receive runtime parameters (profile id, signal id) via the IPC
//     channel from the standalone Tubelight UI.

#include <dlfcn.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <mutex>

// Forward-declare GLX/EGL types without pulling in their headers, so a
// missing X11 or EGL devel package on the build host doesn't fail this
// translation unit. These are opaque at this layer.
extern "C" {

typedef void* TubelightGLXDrawable;
typedef void* TubelightDisplayPtr;
typedef void* TubelightEGLDisplay;
typedef void* TubelightEGLSurface;
typedef unsigned int TubelightEGLBoolean;

}

namespace {

std::atomic<unsigned long long> g_glx_swaps{0};
std::atomic<unsigned long long> g_egl_swaps{0};
std::once_flag g_log_once;

void log_once() {
    std::call_once(g_log_once, []() {
        const char* level = std::getenv("TUBELIGHT_LOG_LEVEL");
        if (level && std::string(level) == "trace") {
            std::fprintf(stderr, "[tubelight-preload] LD_PRELOAD shim active (PID %d)\n",
                         static_cast<int>(getpid()));
        } else {
            std::fprintf(stderr, "[tubelight-preload] active (set TUBELIGHT_LOG_LEVEL=trace for per-call logs)\n");
        }
    });
}

template <typename Fn>
Fn resolve_next(const char* name) {
    void* sym = ::dlsym(RTLD_NEXT, name);
    return reinterpret_cast<Fn>(sym);
}

} // namespace

extern "C" {

// ---- GLX (X11) ----------------------------------------------------------

using glXSwapBuffers_t = void (*)(TubelightDisplayPtr, TubelightGLXDrawable);

void glXSwapBuffers(TubelightDisplayPtr dpy, TubelightGLXDrawable drawable) {
    log_once();
    g_glx_swaps.fetch_add(1, std::memory_order_relaxed);

    static glXSwapBuffers_t real = nullptr;
    if (!real) real = resolve_next<glXSwapBuffers_t>("glXSwapBuffers");
    if (!real) {
        std::fprintf(stderr, "[tubelight-preload] could not resolve real glXSwapBuffers\n");
        return;
    }

    // F7: insert pipeline application here (capture or re-render then swap).
    real(dpy, drawable);
}

// ---- EGL (Wayland / generic) -------------------------------------------

using eglSwapBuffers_t = TubelightEGLBoolean (*)(TubelightEGLDisplay, TubelightEGLSurface);

TubelightEGLBoolean eglSwapBuffers(TubelightEGLDisplay display, TubelightEGLSurface surface) {
    log_once();
    g_egl_swaps.fetch_add(1, std::memory_order_relaxed);

    static eglSwapBuffers_t real = nullptr;
    if (!real) real = resolve_next<eglSwapBuffers_t>("eglSwapBuffers");
    if (!real) {
        std::fprintf(stderr, "[tubelight-preload] could not resolve real eglSwapBuffers\n");
        return 0;
    }

    // F7: insert pipeline application here.
    return real(display, surface);
}

} // extern "C"

// Library destructor — print summary so users can confirm the shim activated.
__attribute__((destructor))
static void tubelight_preload_summary() {
    auto glx = g_glx_swaps.load(std::memory_order_relaxed);
    auto egl = g_egl_swaps.load(std::memory_order_relaxed);
    if (glx || egl) {
        std::fprintf(stderr, "[tubelight-preload] exit: %llu glXSwapBuffers, %llu eglSwapBuffers calls\n",
                     glx, egl);
    }
}
