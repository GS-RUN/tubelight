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
    void build_widgets(Pipeline& pipeline,
                       std::string& current_profile_id,
                       std::string& current_signal_id,
                       float& intensity_multiplier,
                       bool& want_quit);

    // Renders the ImGui draw data on top of whatever the pipeline produced.
    void end_frame_to_screen();

    bool has_imgui() const;

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
