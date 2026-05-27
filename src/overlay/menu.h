// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// In-app overlay menu (Dear ImGui).
//
// The menu is *optional* at build time: when imgui isn't found by CMake,
// TUBELIGHT_HAS_IMGUI is undefined and Menu collapses to a no-op stub so
// the rest of the codebase doesn't have to care.

#pragma once

#include "core/pipeline.h"

#include <string>
#include <vector>

struct GLFWwindow;

namespace tubelight::overlay {

// Output flags the menu uses to request window-level actions the host loop
// has to perform (resizing the GLFW window, swapping decoration styles, etc.).
// Input fields are read by the menu to label / disable controls correctly.
struct WindowActions {
    // Input from host:
    bool is_fullscreen        = false; // overlay is currently borderless fullscreen
    bool is_tracking_target   = false; // overlay is currently following another window
    std::string target_title;          // human-readable title of the current target
    // Output to host:
    bool snap_to_aspect_requested    = false; // resize window to match target_aspect
    bool toggle_fullscreen_requested = false; // flip windowed <-> fullscreen
    bool track_foreground_requested  = false; // attach to whatever has OS focus
    bool track_by_title_requested    = false; // attach to a title-matching window
    bool detach_target_requested     = false; // stop following the current target
    std::string title_to_track;               // input from the text field
    bool save_preset_requested       = false; // serialise current params → user dir
    std::string preset_new_id;                // file stem for new preset
    std::string preset_display_name;          // human label for new preset
    // Region (fixed monitor-relative rect) mode.
    bool is_region_active    = false;
    bool region_attach_requested = false;
    bool region_detach_requested = false;
    int  region_x = 0, region_y = 0, region_w = 0, region_h = 0;
};

class Menu {
public:
    Menu() = default;
    ~Menu() = default;

    Menu(const Menu&) = delete;
    Menu& operator=(const Menu&) = delete;

    // Returns false if ImGui is not compiled in (then is_open() is always false).
    bool init(GLFWwindow* window);
    void shutdown();

    bool is_open() const { return open_; }
    void set_open(bool o) { open_ = o; }
    void toggle()         { open_ = !open_; }

    // Called every frame before any GL rendering. Always cheap; doesn't draw
    // anything yet.
    void begin_frame();

    // Builds the widgets (only if open). Reads + writes pipeline.params() and
    // the currently selected profile / signal ids — caller uses the changed
    // ids to call apply_crt_profile / apply_signal_profile on the pipeline.
    // Settings-IO bundle so build_widgets doesn't grow past 15 params.
    // Each `*_changed` flag is set when the menu mutates the matching
    // field; the host saves once after dispatching all changes.
    struct SettingsIO {
        bool&  hud_visible;
        bool&  hud_changed;
        bool&  audio_enabled;
        float& audio_volume;
        bool&  audio_changed;
        // Click-through for plain windowed mode.
        bool&  clickthrough_user;
        bool&  clickthrough_changed;
        // Video recording source: 0=overlay 1=full monitor 2=custom rect.
        int&   record_source;
        int&   record_rect_x;
        int&   record_rect_y;
        int&   record_rect_w;
        int&   record_rect_h;
        bool&  record_changed;
        // Low-latency mode (vsync off; off by default it already is).
        bool&  low_latency;
        bool&  low_latency_changed;
    };

    void build_widgets(Pipeline& pipeline,
                       std::string& current_profile_id,
                       std::string& current_signal_id,
                       float& intensity_multiplier,
                       bool& want_quit,
                       std::string& capture_dir,
                       bool& capture_dir_changed,
                       WindowActions& window_actions,
                       SettingsIO& sio);

    // Renders the ImGui draw data on top of whatever the pipeline produced.
    void end_frame_to_screen();

    bool has_imgui() const;

    // Forget the cached profile lists so the next build_widgets() call
    // re-scans the profile dirs (used after writing a new user preset
    // so the combo picks it up immediately).
    void invalidate_profile_cache() { profiles_loaded_ = false; crt_ids_.clear(); crt_names_.clear(); sig_ids_.clear(); sig_names_.clear(); }

private:
    bool open_ = false;
    GLFWwindow* window_ = nullptr;

    // Cached profile lists (refreshed on first open).
    std::vector<std::string> crt_ids_;
    std::vector<std::string> crt_names_;
    std::vector<std::string> sig_ids_;
    std::vector<std::string> sig_names_;
    bool profiles_loaded_ = false;
};

} // namespace tubelight::overlay
