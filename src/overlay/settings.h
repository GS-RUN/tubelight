// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// Persistent overlay settings stored as JSON under
//   %APPDATA%\Tubelight\settings.json   (Windows)
//   $XDG_CONFIG_HOME/tubelight/settings.json   (Linux)
//
// Currently holds the user's preferred capture directory; will grow with
// last profile / signal / window position in v1.1.

#pragma once

#include <string>

namespace tubelight::overlay {

struct Settings {
    std::string capture_dir; // empty → default (default_capture_dir())
    bool        hud_visible = false; // status HUD (Ctrl+Alt+H) initial state
    bool        crt_audio_enabled = false; // CRT flyback whine on/off
    float       crt_audio_volume  = 0.20f; // 0..1 master gain
    // Video recording source.
    //   0 = overlay view (glReadPixels from rendered back buffer; matches
    //       what the user sees)
    //   1 = full monitor (DXGI BGRA → ffmpeg, no CRT effect, captures
    //       everything regardless of overlay position/size)
    //   2 = custom monitor-relative rect (uses record_rect_*)
    int  record_source = 0;
    int  record_rect_x = 0;
    int  record_rect_y = 0;
    int  record_rect_w = 1280;
    int  record_rect_h = 720;
    // Click-through on plain windowed mode (Ctrl+Alt+C) — input passes
    // through to whatever's underneath while keeping the CRT effect.
    bool clickthrough_user = false;
    // Low-latency mode: GLFW swap interval 0 (no vsync, may tear).
    // ON by default — most overlay use cases prefer minimum input lag.
    bool low_latency = true;
};

// Reads settings.json if present. Missing fields keep their default values.
Settings load_settings();

// Writes settings to disk (best effort, errors logged to stderr).
void save_settings(const Settings& s);

// Returns the path of settings.json (creates parent dir on demand).
std::string settings_file_path();

} // namespace tubelight::overlay
