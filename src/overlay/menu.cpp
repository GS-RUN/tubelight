// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN

#include "overlay/menu.h"
#include "overlay/folder_picker.h"
#include "profile/profile_loader.h"
#include "profile/validator.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

#ifdef TUBELIGHT_HAS_IMGUI
#include <imgui.h>
// vcpkg installs the backend headers at the include root; upstream Dear
// ImGui ships them in backends/. Pick whichever path resolves.
#if __has_include(<imgui_impl_glfw.h>)
  #include <imgui_impl_glfw.h>
  #include <imgui_impl_opengl3.h>
#else
  #include <backends/imgui_impl_glfw.h>
  #include <backends/imgui_impl_opengl3.h>
#endif
#endif

namespace tubelight::overlay {

namespace fs = std::filesystem;

namespace {

#ifdef TUBELIGHT_HAS_IMGUI

constexpr const char* kPassNames[8] = {
    "Pass -1 (signal)",
    "Pass  0 (analysis)",
    "Pass  1 (dither reconstruct)",
    "Pass  2 (beam + scanlines)",
    "Pass  3 (mask)",
    "Pass  4 (bloom + halation)",
    "Pass  5 (temporal)",
    "Pass  6 (composition)",
};

constexpr const char* kMaskTypeLabels[] = {
    "None", "Shadow Mask", "Aperture Grille", "Slot Mask",
    "Diamond", "CGWG Mix", "Dot Trio"
};

void collect_profiles_in_dirs(const std::vector<std::string>& dirs,
                              ProfileKind expected_kind,
                              std::vector<std::string>& ids,
                              std::vector<std::string>& names) {
    for (const auto& d : dirs) {
        std::error_code ec;
        if (!fs::is_directory(d, ec)) continue;
        for (const auto& entry : fs::directory_iterator(d, ec)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".json") continue;
            ParsedProfile parsed;
            auto vr = validate_profile_file(entry.path().string(), parsed);
            if (!vr.ok || parsed.kind != expected_kind) continue;
            std::string id, name;
            if (expected_kind == ProfileKind::Crt) {
                const auto& p = std::get<CRTProfile>(parsed.data);
                id = p.id;
                name = p.display_name;
            } else {
                const auto& p = std::get<SignalProfile>(parsed.data);
                id = p.id;
                name = p.display_name;
            }
            ids.push_back(std::move(id));
            names.push_back(std::move(name));
        }
    }
    // Sort by id for deterministic combo order.
    std::vector<size_t> order(ids.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(),
              [&](size_t a, size_t b){ return ids[a] < ids[b]; });
    std::vector<std::string> ids_sorted(ids.size()), names_sorted(names.size());
    for (size_t i = 0; i < order.size(); ++i) {
        ids_sorted[i] = std::move(ids[order[i]]);
        names_sorted[i] = std::move(names[order[i]]);
    }
    ids = std::move(ids_sorted);
    names = std::move(names_sorted);
}

#endif // TUBELIGHT_HAS_IMGUI

} // namespace

bool Menu::has_imgui() const {
#ifdef TUBELIGHT_HAS_IMGUI
    return true;
#else
    return false;
#endif
}

bool Menu::init(GLFWwindow* window) {
#ifdef TUBELIGHT_HAS_IMGUI
    window_ = window;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // don't write imgui.ini next to the exe
    ImGui::StyleColorsDark();

    // Slightly translucent + larger font scale for fullscreen overlay use.
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding   = 6.0f;
    style.FrameRounding    = 4.0f;
    style.ScrollbarRounding= 6.0f;
    style.Colors[ImGuiCol_WindowBg].w = 0.86f;
    io.FontGlobalScale = 1.25f;

    if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) {
        std::fprintf(stderr, "[menu] ImGui_ImplGlfw_InitForOpenGL failed\n");
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 450 core")) {
        std::fprintf(stderr, "[menu] ImGui_ImplOpenGL3_Init failed\n");
        return false;
    }
    return true;
#else
    (void)window;
    return false;
#endif
}

void Menu::shutdown() {
#ifdef TUBELIGHT_HAS_IMGUI
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
#endif
}

void Menu::begin_frame() {
#ifdef TUBELIGHT_HAS_IMGUI
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
#endif
}

void Menu::build_widgets(Pipeline& pipeline,
                         std::string& current_profile_id,
                         std::string& current_signal_id,
                         float& intensity_multiplier,
                         bool& want_quit,
                         std::string& capture_dir,
                         bool& capture_dir_changed,
                         WindowActions& window_actions,
                         bool& hud_visible,
                         bool& hud_changed) {
    capture_dir_changed = false;
    hud_changed = false;
    window_actions.snap_to_aspect_requested = false;
    window_actions.toggle_fullscreen_requested = false;
    window_actions.track_foreground_requested = false;
    window_actions.track_by_title_requested = false;
    window_actions.detach_target_requested = false;
    window_actions.save_preset_requested = false;
#ifdef TUBELIGHT_HAS_IMGUI
    if (!open_) return;

    if (!profiles_loaded_) {
        collect_profiles_in_dirs(default_crt_profile_dirs(),
                                 ProfileKind::Crt, crt_ids_, crt_names_);
        collect_profiles_in_dirs(default_signal_profile_dirs(),
                                 ProfileKind::Signal, sig_ids_, sig_names_);
        profiles_loaded_ = true;
    }

    ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(460, 720), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("Tubelight — overlay controls",
                      &open_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Ctrl+Alt+M  toggle menu");
    ImGui::TextDisabled("Ctrl+Alt+Q  quit  |  Ctrl+Alt+F  freeze  |  Ctrl+Alt+1..8  toggle pass");
    ImGui::Separator();

    // --- Profiles ----------------------------------------------------
    if (ImGui::CollapsingHeader("Profiles", ImGuiTreeNodeFlags_DefaultOpen)) {
        int crt_idx = -1;
        for (size_t i = 0; i < crt_ids_.size(); ++i)
            if (crt_ids_[i] == current_profile_id) { crt_idx = static_cast<int>(i); break; }
        std::vector<const char*> crt_items(crt_names_.size());
        for (size_t i = 0; i < crt_names_.size(); ++i) crt_items[i] = crt_names_[i].c_str();
        if (ImGui::Combo("CRT", &crt_idx, crt_items.data(),
                         static_cast<int>(crt_items.size()))) {
            if (crt_idx >= 0 && crt_idx < static_cast<int>(crt_ids_.size())) {
                current_profile_id = crt_ids_[crt_idx];
            }
        }

        // Signal combo: only meaningful for colour CRTs. Monochrome
        // terminals are locked to a clean RGB-direct signal path (P31
        // green / amber / Mac Classic / B&W); showing the picker would
        // suggest the user can change it, but apply_signal_profile
        // ignores the request while monochrome is active.
        if (pipeline.params().monochrome == 0) {
            int sig_idx = -1;
            for (size_t i = 0; i < sig_ids_.size(); ++i)
                if (sig_ids_[i] == current_signal_id) { sig_idx = static_cast<int>(i); break; }
            std::vector<const char*> sig_items(sig_names_.size());
            for (size_t i = 0; i < sig_names_.size(); ++i) sig_items[i] = sig_names_[i].c_str();
            if (ImGui::Combo("Signal", &sig_idx, sig_items.data(),
                             static_cast<int>(sig_items.size()))) {
                if (sig_idx >= 0 && sig_idx < static_cast<int>(sig_ids_.size())) {
                    current_signal_id = sig_ids_[sig_idx];
                }
            }
        } else {
            ImGui::TextDisabled("Signal: clean RGB (locked for monochrome)");
        }

        // ---- Save current settings as a new preset --------------------
        if (ImGui::TreeNode("Save current as preset…")) {
            static char id_buf[128];
            static char name_buf[256];
            ImGui::TextDisabled("Writes %%APPDATA%%\\Tubelight\\profiles\\crts\\<id>.json");
            ImGui::InputText("id (filename)",  id_buf,  sizeof(id_buf));
            ImGui::InputText("display name",   name_buf, sizeof(name_buf));
            if (ImGui::Button("Save preset", ImVec2(-1, 0))) {
                window_actions.preset_new_id        = id_buf;
                window_actions.preset_display_name  = name_buf;
                window_actions.save_preset_requested = true;
                id_buf[0] = 0; name_buf[0] = 0; // clear for next time
            }
            ImGui::TreePop();
        }
    }

    // --- Target window (track another app's window) -----------------
    if (ImGui::CollapsingHeader("Target window")) {
        if (window_actions.is_tracking_target) {
            ImGui::Text("Tracking: %s",
                        window_actions.target_title.empty()
                            ? "(unknown title)"
                            : window_actions.target_title.c_str());
            ImGui::TextDisabled("Tubelight follows this window. Click-through is on,");
            ImGui::TextDisabled("so the underlying app keeps full focus + input.");
            if (ImGui::Button("Detach (Ctrl+Alt+T)", ImVec2(-1, 0))) {
                window_actions.detach_target_requested = true;
            }
        } else {
            static char title_buf[256];
            ImGui::TextDisabled("Type a window title substring (case-insensitive)");
            ImGui::InputText("##targettitle", title_buf, sizeof(title_buf));
            if (ImGui::Button("Track by title", ImVec2(150, 0))) {
                window_actions.title_to_track = title_buf;
                window_actions.track_by_title_requested = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Track foreground (Ctrl+Alt+T)", ImVec2(-1, 0))) {
                window_actions.track_foreground_requested = true;
            }
            ImGui::TextDisabled("Foreground = whatever window had focus before menu.");
        }
    }

    // --- Intensity ---------------------------------------------------
    if (ImGui::CollapsingHeader("Global intensity", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Intensity ×", &intensity_multiplier, 0.0f, 2.0f, "%.2f");
        ImGui::TextDisabled("0 = passthrough, 1 = desktop default, 2 = full retro");
    }

    auto& P = pipeline.params();
    const bool mono_locked = (P.monochrome == 1);

    // Scanlines / mask / bloom / composition sliders only matter for
    // colour CRTs. For monochrome profiles the locked preset already
    // set them to known-good values; exposing the sliders just lets
    // the user accidentally break the look. Show a one-liner instead.
    if (mono_locked) {
        ImGui::TextDisabled("Monochrome preset locked. Only Intensity adjustable.");
        ImGui::TextDisabled("Switch CRT to a colour profile to unlock sliders.");
    }

    if (!mono_locked && ImGui::CollapsingHeader("Scanlines / beam", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Scanline strength", &P.scanline_strength, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Beam width",        &P.beam_width,        0.5f, 3.0f, "%.2f");
        ImGui::SliderFloat("CRT gamma",         &P.gamma_crt,         1.8f, 3.0f, "%.2f");
        ImGui::SliderFloat("Scanline count",    &P.scanline_count,    60.0f, 800.0f, "%.0f");
        ImGui::TextDisabled("240=NTSC, 288=PAL, 480=VGA");
    }

    if (!mono_locked && ImGui::CollapsingHeader("Phosphor mask")) {
        ImGui::Combo("Type", &P.mask_type, kMaskTypeLabels, 7);
        ImGui::SliderFloat("Strength",  &P.mask_strength,  0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Pitch (px)", &P.mask_pitch_px, 1.0f, 10.0f, "%.1f");
    }

    if (!mono_locked && ImGui::CollapsingHeader("Bloom / halation")) {
        ImGui::SliderFloat("Bloom",    &P.bloom_strength,    0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Halation", &P.halation_strength, 0.0f, 1.0f, "%.2f");
    }

    if (!mono_locked && ImGui::CollapsingHeader("Phosphor persistence")) {
        ImGui::SliderFloat("Strength",  &P.persistence_strength, 0.0f, 0.95f, "%.2f");
        ImGui::SliderFloat("R ratio",   &P.persistence_ratio_r,  0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("G ratio",   &P.persistence_ratio_g,  0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("B ratio",   &P.persistence_ratio_b,  0.0f, 1.0f, "%.2f");
        ImGui::TextDisabled("P22 colour CRT: R~1.0, G~0.5, B~0.5 (warm trail)");
    }

    if (ImGui::CollapsingHeader("Composition")) {
        if (!mono_locked) {
            ImGui::SliderFloat("Barrel",        &P.barrel_strength,   0.0f, 0.20f, "%.3f");
            ImGui::SliderFloat("Vignette",      &P.vignette_strength, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Display gamma", &P.gamma_display,     1.8f, 3.0f, "%.2f");
        }

        // Aspect ratio override. Picking an option overwrites target_aspect;
        // selecting a different CRT profile after will re-derive from its
        // aspect_native field, so the user is always in control.
        struct AspectOpt { const char* label; float ar; };
        static const AspectOpt kAspects[] = {
            { "Fill window (no bars)",          0.0f          },
            { "4:3   (PVM / vintage TV)",       4.0f/3.0f     },
            { "5:4   (NEC / IBM 5151)",         5.0f/4.0f     },
            { "16:10 (early widescreen)",       16.0f/10.0f   },
            { "16:9  (modern widescreen)",      16.0f/9.0f    },
            { "21:9  (ultrawide CRT FW900)",    21.0f/9.0f    },
        };
        const int n_aspects = static_cast<int>(sizeof(kAspects) / sizeof(kAspects[0]));
        int idx = 0;
        for (int i = 0; i < n_aspects; ++i) {
            if (std::abs(kAspects[i].ar - P.target_aspect) < 0.01f) { idx = i; break; }
        }
        std::vector<const char*> labels;
        labels.reserve(static_cast<size_t>(n_aspects));
        for (int i = 0; i < n_aspects; ++i) labels.push_back(kAspects[i].label);
        if (ImGui::Combo("Aspect ratio", &idx, labels.data(), n_aspects)) {
            P.target_aspect = kAspects[idx].ar;
            // Picking a non-Fill aspect implicitly asks the host to snap the
            // window to that shape so the picture fills it edge to edge
            // (no letterbox bars) — matches the user's mental model.
            if (P.target_aspect > 0.0f) {
                window_actions.snap_to_aspect_requested = true;
            }
        }
        ImGui::TextDisabled("Default comes from CRT profile (aspect_native)");

        // Window-level actions: snap window to the chosen aspect, and toggle
        // borderless fullscreen on the current monitor. Fullscreen keeps the
        // same target_aspect (no stretching — black bars handle the rest).
        if (ImGui::Button("Snap window to aspect", ImVec2(180, 0))) {
            if (P.target_aspect > 0.0f) {
                window_actions.snap_to_aspect_requested = true;
            }
        }
        ImGui::SameLine();
        const char* fs_label = window_actions.is_fullscreen
                                ? "Exit fullscreen (Ctrl+Alt+Enter)"
                                : "Go fullscreen (Ctrl+Alt+Enter)";
        if (ImGui::Button(fs_label, ImVec2(-1, 0))) {
            window_actions.toggle_fullscreen_requested = true;
        }
        ImGui::TextDisabled("Fullscreen preserves aspect ratio (letterbox)");
    }

    if (!mono_locked && ImGui::CollapsingHeader("Pass toggles")) {
        for (int i = 0; i < Pipeline::kPassCount; ++i) {
            bool e = pipeline.is_pass_enabled(i);
            if (ImGui::Checkbox(kPassNames[i], &e)) {
                pipeline.set_pass_enabled(i, e);
            }
        }
    }

    if (ImGui::CollapsingHeader("Captures (screenshots + video)", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Editable capture directory. We mutate the std::string via a
        // fixed-size InputText buffer to avoid wrestling with ImGui's
        // callback-based variant.
        static char buf[512];
        if (buf[0] == 0 || std::string(buf) != capture_dir) {
            std::snprintf(buf, sizeof(buf), "%s", capture_dir.c_str());
        }
        ImGui::TextDisabled("Folder where Ctrl+Alt+S / Ctrl+Alt+V save");
        if (ImGui::InputText("##capdir", buf, sizeof(buf))) {
            // edited
        }
        if (ImGui::Button("Browse...", ImVec2(110, 0))) {
            std::string picked = browse_for_folder("Choose Tubelight capture folder");
            if (!picked.empty()) {
                capture_dir = picked;
                std::snprintf(buf, sizeof(buf), "%s", picked.c_str());
                capture_dir_changed = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply", ImVec2(80, 0))) {
            capture_dir = buf;
            capture_dir_changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Default", ImVec2(90, 0))) {
            capture_dir.clear();
            std::snprintf(buf, sizeof(buf), "%s", "");
            capture_dir_changed = true;
        }
        ImGui::TextDisabled("Ctrl+Alt+S  screenshot  |  Ctrl+Alt+V  toggle video");

        ImGui::Separator();
        if (ImGui::Checkbox("Show status HUD (top-right) — Ctrl+Alt+H", &hud_visible)) {
            hud_changed = true;
        }
        ImGui::TextDisabled("Persisted between launches");
    }

    ImGui::Separator();
    if (ImGui::Button("Hide menu (Ctrl+Alt+M)", ImVec2(-1, 0))) {
        open_ = false;
    }
    if (ImGui::Button("Quit overlay (Ctrl+Alt+Q)", ImVec2(-1, 0))) {
        want_quit = true;
    }

    ImGui::End();
#else
    (void)pipeline;
    (void)current_profile_id;
    (void)current_signal_id;
    (void)intensity_multiplier;
    (void)want_quit;
    (void)capture_dir;
    (void)capture_dir_changed;
    (void)window_actions;
    (void)hud_visible;
    (void)hud_changed;
#endif
}

void Menu::end_frame_to_screen() {
#ifdef TUBELIGHT_HAS_IMGUI
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif
}

} // namespace tubelight::overlay
