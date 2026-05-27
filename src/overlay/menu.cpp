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

// Apply the phosphor-green-on-black "CRT terminal" colour theme + a
// terminal-flat 1 px rounding style. Called once at init. Designed to
// match the Tubelight branding (deep CRT black + saturated phosphor
// green primary + amber accent for hot/active states).
void apply_tubelight_theme(ImGuiStyle& style) {
    auto& c = style.Colors;
    const ImVec4 phos       = ImVec4(0.00f, 1.00f, 0.255f, 1.00f); // #00FF41
    const ImVec4 phos_dim   = ImVec4(0.00f, 0.65f, 0.16f,  1.00f);
    const ImVec4 amber      = ImVec4(1.00f, 0.69f, 0.00f,  1.00f); // #FFB000
    const ImVec4 amber_dim  = ImVec4(0.55f, 0.38f, 0.00f,  1.00f);
    const ImVec4 bg         = ImVec4(0.02f, 0.03f, 0.02f,  0.93f);
    const ImVec4 bg_frame   = ImVec4(0.04f, 0.07f, 0.04f,  1.00f);
    const ImVec4 bg_hover   = ImVec4(0.06f, 0.12f, 0.06f,  1.00f);
    const ImVec4 bg_active  = ImVec4(0.08f, 0.18f, 0.08f,  1.00f);
    const ImVec4 sep        = ImVec4(0.00f, 0.35f, 0.10f,  1.00f);
    const ImVec4 text_dim   = ImVec4(0.00f, 0.55f, 0.14f,  1.00f);

    c[ImGuiCol_WindowBg]            = bg;
    c[ImGuiCol_ChildBg]             = bg;
    c[ImGuiCol_PopupBg]             = ImVec4(0.03f, 0.06f, 0.03f, 0.96f);
    c[ImGuiCol_Border]              = sep;
    c[ImGuiCol_FrameBg]             = bg_frame;
    c[ImGuiCol_FrameBgHovered]      = bg_hover;
    c[ImGuiCol_FrameBgActive]       = bg_active;
    c[ImGuiCol_TitleBg]             = ImVec4(0.04f, 0.10f, 0.04f, 1.00f);
    c[ImGuiCol_TitleBgActive]       = ImVec4(0.06f, 0.18f, 0.06f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]    = ImVec4(0.03f, 0.07f, 0.03f, 1.00f);
    c[ImGuiCol_MenuBarBg]           = ImVec4(0.03f, 0.07f, 0.03f, 1.00f);
    c[ImGuiCol_ScrollbarBg]         = bg_frame;
    c[ImGuiCol_ScrollbarGrab]       = phos_dim;
    c[ImGuiCol_ScrollbarGrabHovered]= phos;
    c[ImGuiCol_ScrollbarGrabActive] = amber;
    c[ImGuiCol_CheckMark]           = amber;
    c[ImGuiCol_SliderGrab]          = phos_dim;
    c[ImGuiCol_SliderGrabActive]    = amber;
    c[ImGuiCol_Button]              = ImVec4(0.04f, 0.18f, 0.06f, 1.00f);
    c[ImGuiCol_ButtonHovered]       = ImVec4(0.06f, 0.28f, 0.09f, 1.00f);
    c[ImGuiCol_ButtonActive]        = amber_dim;
    c[ImGuiCol_Header]              = ImVec4(0.04f, 0.14f, 0.05f, 1.00f);
    c[ImGuiCol_HeaderHovered]       = ImVec4(0.06f, 0.22f, 0.08f, 1.00f);
    c[ImGuiCol_HeaderActive]        = ImVec4(0.08f, 0.30f, 0.10f, 1.00f);
    c[ImGuiCol_Separator]           = sep;
    c[ImGuiCol_SeparatorHovered]    = phos;
    c[ImGuiCol_SeparatorActive]     = amber;
    c[ImGuiCol_Tab]                 = ImVec4(0.03f, 0.10f, 0.04f, 1.00f);
    c[ImGuiCol_TabHovered]          = ImVec4(0.06f, 0.22f, 0.08f, 1.00f);
    c[ImGuiCol_TabActive]           = ImVec4(0.05f, 0.20f, 0.07f, 1.00f);
    c[ImGuiCol_TabUnfocused]        = ImVec4(0.02f, 0.07f, 0.03f, 1.00f);
    c[ImGuiCol_TabUnfocusedActive]  = ImVec4(0.04f, 0.14f, 0.05f, 1.00f);
    c[ImGuiCol_Text]                = phos;
    c[ImGuiCol_TextDisabled]        = text_dim;
    c[ImGuiCol_TextSelectedBg]      = ImVec4(0.00f, 0.35f, 0.10f, 0.5f);

    style.WindowRounding    = 1.0f;
    style.FrameRounding     = 1.0f;
    style.GrabRounding      = 1.0f;
    style.ScrollbarRounding = 1.0f;
    style.TabRounding       = 0.0f;
    style.PopupRounding     = 1.0f;
    style.FramePadding      = ImVec2(6, 4);
    style.ItemSpacing       = ImVec2(8, 6);
    style.ItemInnerSpacing  = ImVec2(6, 4);
    style.WindowPadding     = ImVec2(10, 10);
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.TabBorderSize     = 0.0f;
    style.GrabMinSize       = 10.0f;
}

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
    // CRT-themed look (phosphor-green on black + amber accent) +
    // 1-px corners for a terminal feel.
    apply_tubelight_theme(ImGui::GetStyle());
    io.FontGlobalScale = 1.18f;

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
                         SettingsIO& sio) {
    capture_dir_changed = false;
    sio.hud_changed = false;
    sio.audio_changed = false;
    sio.clickthrough_changed = false;
    sio.record_changed = false;
    sio.low_latency_changed = false;
    sio.recordable_changed = false;
    bool& hud_visible    = sio.hud_visible;
    bool& audio_enabled  = sio.audio_enabled;
    float& audio_volume  = sio.audio_volume;
    bool& hud_changed    = sio.hud_changed;
    bool& audio_changed  = sio.audio_changed;
    window_actions.snap_to_aspect_requested = false;
    window_actions.toggle_fullscreen_requested = false;
    window_actions.track_foreground_requested = false;
    window_actions.track_by_title_requested = false;
    window_actions.detach_target_requested = false;
    window_actions.save_preset_requested = false;
    window_actions.region_attach_requested = false;
    window_actions.region_detach_requested = false;
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
    ImGui::SetNextWindowSize(ImVec2(520, 760), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(440, 420), ImVec2(900, 1400));

    if (!ImGui::Begin("TUBELIGHT // overlay control",
                      &open_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    auto& P = pipeline.params();
    const bool mono_locked = (P.monochrome == 1);

    if (ImGui::BeginTabBar("##tubelight_tabs", ImGuiTabBarFlags_None)) {

        // ====================== PROFILE TAB ======================
        if (ImGui::BeginTabItem("Profile")) {
            // CRT combo
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
            ImGui::SetItemTooltip("Perfil de tubo CRT: define mascara, fosforo, sombra y aspecto.\n"
                                  "Cambiar reemplaza todos los valores del bloque Image.");

            // Signal combo (only colour CRTs)
            if (!mono_locked) {
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
                ImGui::SetItemTooltip("Perfil de senal: simula NTSC / PAL / RGB / composite.\n"
                                      "Bloqueado en monocromo (siempre RGB limpio).");
            } else {
                ImGui::TextDisabled("Signal: clean RGB (locked for monochrome)");
            }

            ImGui::Separator();
            ImGui::SliderFloat("Intensity x", &intensity_multiplier, 0.0f, 2.0f, "%.2f");
            ImGui::SetItemTooltip("Mezcla global con la imagen original.\n"
                                  "0=passthrough sin efecto, 1=normal, 2=maximo retro.\n"
                                  "Default: 1.0");

            ImGui::Separator();
            if (ImGui::TreeNode("Save current as preset...")) {
                static char id_buf[128];
                static char name_buf[256];
                ImGui::TextDisabled("Guarda en %%APPDATA%%\\Tubelight\\profiles\\crts\\<id>.json");
                ImGui::InputText("id (filename)",  id_buf,  sizeof(id_buf));
                ImGui::SetItemTooltip("Nombre de archivo (sin .json). Solo a-z, 0-9, guiones.\n"
                                      "Ej: mi_pvm_verde");
                ImGui::InputText("display name",   name_buf, sizeof(name_buf));
                ImGui::SetItemTooltip("Nombre visible en el combo CRT. Texto libre.\n"
                                      "Ej: Mi PVM verde");
                if (ImGui::Button("Save preset", ImVec2(-1, 0))) {
                    window_actions.preset_new_id        = id_buf;
                    window_actions.preset_display_name  = name_buf;
                    window_actions.save_preset_requested = true;
                    id_buf[0] = 0; name_buf[0] = 0;
                }
                ImGui::SetItemTooltip("Guarda los valores actuales como nuevo preset.");
                ImGui::TreePop();
            }
            ImGui::EndTabItem();
        }

        // ====================== IMAGE TAB ======================
        if (ImGui::BeginTabItem("Image")) {
            if (mono_locked) {
                ImGui::TextDisabled("Monochrome tube: mask + per-channel persistence hidden.");
                ImGui::TextDisabled("(single-phosphor tubes have no mask). Tint + glow below.");
            }

            // ---- Scanlines / beam ----
            if (ImGui::CollapsingHeader("Scanlines / beam", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SliderFloat("Scanline strength", &P.scanline_strength, 0.0f, 1.0f, "%.2f");
                ImGui::SetItemTooltip("Cuan oscuras son las lineas entre escaneos.\n"
                                      "0=invisibles, 1=PVM agresivo. Default ~0.35.");
                ImGui::SliderFloat("Beam width",        &P.beam_width,        0.5f, 3.0f, "%.2f");
                ImGui::SetItemTooltip("Grosor del haz electronico.\n"
                                      "<1=fino y nitido, >2=grueso y borroso (TV vieja). Default 1.30.");
                ImGui::SliderFloat("CRT gamma",         &P.gamma_crt,         1.8f, 3.0f, "%.2f");
                ImGui::SetItemTooltip("Linealizacion de entrada.\n"
                                      "2.2=sRGB neutro, 2.5=mas contraste. Default 2.2.");
                ImGui::SliderFloat("Scanline count",    &P.scanline_count,    60.0f, 800.0f, "%.0f");
                ImGui::SetItemTooltip("Lineas visibles por frame.\n"
                                      "240=NTSC, 288=PAL, 350=terminal IBM, 480=VGA.");
            }

            // ---- Phosphor mask (colour CRT) ----
            if (!mono_locked && ImGui::CollapsingHeader("Phosphor mask (colour CRT)")) {
                ImGui::Combo("Type", &P.mask_type, kMaskTypeLabels, 7);
                ImGui::SetItemTooltip("Geometria de fosforos:\n"
                                      "Shadow=triadico TV consumer, Aperture=Trinitron rayas verticales,\n"
                                      "Slot=mezcla (1084S), Diamond=PVM Sony.");
                ImGui::SliderFloat("Strength",  &P.mask_strength,  0.0f, 1.0f, "%.2f");
                ImGui::SetItemTooltip("Cuan visible es la rejilla de subpixeles RGB.\n"
                                      "0=invisible, 1=cada subpixel separado. Default 0.22.");
                ImGui::SliderFloat("Pitch (px)", &P.mask_pitch_px, 1.0f, 10.0f, "%.1f");
                ImGui::SetItemTooltip("Tamano de la triada en pixeles de pantalla.\n"
                                      "3=denso, 8=PVM gigante. Default 3.0.");
            }

            // ---- Phosphor colour (monochrome) ----
            if (mono_locked && ImGui::CollapsingHeader("Phosphor colour (monochrome)",
                                                        ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SliderFloat("R tint", &P.phosphor_color_r, 0.0f, 1.5f, "%.2f");
                ImGui::SetItemTooltip("Componente rojo del fosforo monocromo.\n"
                                      "0.05=P31 verde, 1.30=P3 ambar, 1.0=P4 blanco.");
                ImGui::SliderFloat("G tint", &P.phosphor_color_g, 0.0f, 1.5f, "%.2f");
                ImGui::SetItemTooltip("Componente verde. P31=1.30, P3=0.55, P4=1.00.");
                ImGui::SliderFloat("B tint", &P.phosphor_color_b, 0.0f, 1.5f, "%.2f");
                ImGui::SetItemTooltip("Componente azul. P31=0.18, P3=0.05, P4=1.10.");
                if (ImGui::Button("P31 green")) {
                    P.phosphor_color_r = 0.12f; P.phosphor_color_g = 1.30f; P.phosphor_color_b = 0.18f;
                }
                ImGui::SetItemTooltip("Verde puro de mainframe / Apple IIe / VT100.");
                ImGui::SameLine();
                if (ImGui::Button("P3 amber")) {
                    P.phosphor_color_r = 1.30f; P.phosphor_color_g = 0.55f; P.phosphor_color_b = 0.05f;
                }
                ImGui::SetItemTooltip("Ambar IBM 5151 / WYSE — calido para sesiones largas.");
                ImGui::SameLine();
                if (ImGui::Button("P1 oscope")) {
                    P.phosphor_color_r = 0.05f; P.phosphor_color_g = 1.35f; P.phosphor_color_b = 0.20f;
                }
                ImGui::SetItemTooltip("Verde-amarillo de osciloscopio Tektronix.");
                ImGui::SameLine();
                if (ImGui::Button("P4 white")) {
                    P.phosphor_color_r = 0.92f; P.phosphor_color_g = 1.00f; P.phosphor_color_b = 1.10f;
                }
                ImGui::SetItemTooltip("Blanco neutro: TV B&N + Mac Classic.");
                ImGui::SliderInt("Posterize levels", &P.posterize_levels, 0, 8);
                ImGui::SetItemTooltip("Cuantizacion de luminancia.\n"
                                      "0=continuo analogico, 2=1-bit Mac Classic, 4-6=terminal de texto.");
            }

            // ---- Bloom / glow ----
            if (!mono_locked && ImGui::CollapsingHeader("Bloom / halation")) {
                ImGui::SliderFloat("Bloom",    &P.bloom_strength,    0.0f, 1.0f, "%.2f");
                ImGui::SetItemTooltip("Sangrado de zonas brillantes a oscuras\n"
                                      "(sobreexcitacion del haz electronico).");
                ImGui::SliderFloat("Halation", &P.halation_strength, 0.0f, 1.0f, "%.2f");
                ImGui::SetItemTooltip("Aro rojizo alrededor de blancos\n"
                                      "(luz rebotando dentro del vidrio del tubo).");
            }
            if (mono_locked && ImGui::CollapsingHeader("Phosphor glow")) {
                ImGui::SliderFloat("Bloom",    &P.bloom_strength,    0.0f, 1.0f, "%.2f");
                ImGui::SetItemTooltip("Bloom del fosforo. Mono tubes solo bloom, no halacion.");
            }

            // ---- Persistence ----
            if (!mono_locked && ImGui::CollapsingHeader("Phosphor persistence (per-channel)")) {
                ImGui::SliderFloat("Strength",  &P.persistence_strength, 0.0f, 0.95f, "%.2f");
                ImGui::SetItemTooltip("Cuanto rastro deja el fosforo entre frames.\n"
                                      "0=sin rastro, 0.95=largo (osciloscopio).");
                ImGui::SliderFloat("R ratio",   &P.persistence_ratio_r,  0.0f, 1.0f, "%.2f");
                ImGui::SetItemTooltip("Decaimiento relativo del rojo. P22 colour: ~1.0 (rastro calido).");
                ImGui::SliderFloat("G ratio",   &P.persistence_ratio_g,  0.0f, 1.0f, "%.2f");
                ImGui::SetItemTooltip("Decaimiento relativo del verde. P22 tipico: 0.5.");
                ImGui::SliderFloat("B ratio",   &P.persistence_ratio_b,  0.0f, 1.0f, "%.2f");
                ImGui::SetItemTooltip("Decaimiento relativo del azul. P22 tipico: 0.5.");
                ImGui::TextDisabled("P22 colour CRT: R~1.0, G~0.5, B~0.5 (warm trail)");
            }
            if (mono_locked && ImGui::CollapsingHeader("Phosphor persistence (afterglow)")) {
                ImGui::SliderFloat("Strength", &P.persistence_strength, 0.0f, 0.95f, "%.2f");
                ImGui::SetItemTooltip("Persistencia del fosforo monocromo.\n"
                                      "P31/P4 cortos (~0). P3 medio (~0.3). P1 largo (~0.7).");
            }

            // ---- Composition ----
            if (ImGui::CollapsingHeader("Composition")) {
                if (!mono_locked) {
                    ImGui::SliderFloat("Barrel",        &P.barrel_strength,   0.0f, 0.20f, "%.3f");
                    ImGui::SetItemTooltip("Distorsion de barril (curvatura del tubo).\n"
                                          "0=plano, 0.05=PVM, 0.20=TV redondeada.");
                    ImGui::SliderFloat("Vignette",      &P.vignette_strength, 0.0f, 1.0f, "%.2f");
                    ImGui::SetItemTooltip("Oscurecimiento de las esquinas.");
                    ImGui::SliderFloat("Display gamma", &P.gamma_display,     1.8f, 3.0f, "%.2f");
                    ImGui::SetItemTooltip("Gamma final hacia tu monitor. 2.2=sRGB. Default 2.2.");
                }

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
                    if (P.target_aspect > 0.0f) {
                        window_actions.snap_to_aspect_requested = true;
                    }
                }
                ImGui::SetItemTooltip("Forma del area de imagen.\n"
                                      "Fill=sin barras, 4:3=TV vintage, 16:9=moderno.\n"
                                      "Default viene del perfil CRT (aspect_native).");

                if (ImGui::Button("Snap window to aspect", ImVec2(180, 0))) {
                    if (P.target_aspect > 0.0f) {
                        window_actions.snap_to_aspect_requested = true;
                    }
                }
                ImGui::SetItemTooltip("Redimensiona la ventana de Tubelight\n"
                                      "para que la imagen llene sin barras.");
                ImGui::SameLine();
                const char* fs_label = window_actions.is_fullscreen
                                        ? "Exit fullscreen"
                                        : "Go fullscreen";
                if (ImGui::Button(fs_label, ImVec2(-1, 0))) {
                    window_actions.toggle_fullscreen_requested = true;
                }
                ImGui::SetItemTooltip("Pantalla completa borderless en el monitor actual.\n"
                                      "Mantiene aspect ratio (letterbox).\n"
                                      "Atajo: Ctrl+Alt+Enter");

                static const char* kBezelLabels[6] = {
                    "None (black bars)",
                    "PVM matte black metal",
                    "Beige terminal plastic",
                    "B&W TV wood console",
                    "Compact Mac white plastic",
                    "Generic dark grey",
                };
                int bz = std::clamp(P.bezel_style, 0, 5);
                if (ImGui::Combo("Bezel style", &bz, kBezelLabels, 6)) {
                    P.bezel_style = bz;
                }
                ImGui::SetItemTooltip("Marco dibujado en las barras laterales.\n"
                                      "Default viene del perfil CRT.");
            }

            // ---- Pass toggles ----
            if (ImGui::CollapsingHeader("Pass toggles (advanced)")) {
                static const char* kPassTooltips[8] = {
                    "Modela el cable (NTSC/PAL/composite). Off=RGB perfecto.",
                    "Mide luminancia. Off rompe voltage-bloom y audio modulado.",
                    "Reconstruye gradientes de imagenes ditheradas (NES/Genesis).",
                    "Lineas de escaneo + linealizacion gamma. Pass principal.",
                    "Mascara de fosforos RGB. Off=sin triada.",
                    "Bloom y halo rojo.",
                    "Persistencia del fosforo (rastro).",
                    "Barril, vignette, gamma final, marco. Casi siempre necesario.",
                };
                for (int i = 0; i < Pipeline::kPassCount; ++i) {
                    bool e = pipeline.is_pass_enabled(i);
                    if (ImGui::Checkbox(kPassNames[i], &e)) {
                        pipeline.set_pass_enabled(i, e);
                    }
                    ImGui::SetItemTooltip("%s", kPassTooltips[i]);
                }
            }
            ImGui::EndTabItem();
        }

        // ====================== CAPTURE TAB ======================
        if (ImGui::BeginTabItem("Capture")) {
            // ---- Target window ----
            if (ImGui::CollapsingHeader("Target window")) {
                if (window_actions.is_tracking_target) {
                    ImGui::Text("Tracking: %s",
                                window_actions.target_title.empty()
                                    ? "(unknown title)"
                                    : window_actions.target_title.c_str());
                    ImGui::TextDisabled("Tubelight follows this window with click-through.");
                    if (ImGui::Button("Detach", ImVec2(-1, 0))) {
                        window_actions.detach_target_requested = true;
                    }
                    ImGui::SetItemTooltip("Suelta la ventana seguida. Atajo: Ctrl+Alt+T");
                } else {
                    static char title_buf[256];
                    ImGui::InputText("##targettitle", title_buf, sizeof(title_buf));
                    ImGui::SetItemTooltip("Substring del titulo a seguir (case-insensitive).\n"
                                          "Ej: notepad");
                    if (ImGui::Button("Track by title", ImVec2(150, 0))) {
                        window_actions.title_to_track = title_buf;
                        window_actions.track_by_title_requested = true;
                    }
                    ImGui::SetItemTooltip("Engancha a la ventana cuyo titulo contiene el texto.");
                    ImGui::SameLine();
                    if (ImGui::Button("Track foreground", ImVec2(-1, 0))) {
                        window_actions.track_foreground_requested = true;
                    }
                    ImGui::SetItemTooltip("Engancha a la ventana que tenia foco antes del menu.\n"
                                          "Atajo: Ctrl+Alt+T");
                }
            }

            // ---- Region ----
            if (ImGui::CollapsingHeader("Region (fixed rect)")) {
                if (window_actions.is_region_active) {
                    ImGui::TextDisabled("Pinned to a fixed monitor-relative rect.");
                    if (ImGui::Button("Detach region", ImVec2(-1, 0))) {
                        window_actions.region_detach_requested = true;
                    }
                    ImGui::SetItemTooltip("Suelta el rectangulo fijo.");
                } else {
                    static int rx = 100, ry = 100, rw = 800, rh = 600;
                    ImGui::InputInt("x##region", &rx);
                    ImGui::SetItemTooltip("Coordenada X en pixeles del monitor (0=borde izquierdo).");
                    ImGui::InputInt("y##region", &ry);
                    ImGui::SetItemTooltip("Coordenada Y en pixeles del monitor (0=borde superior).");
                    ImGui::InputInt("w##region", &rw);
                    ImGui::SetItemTooltip("Ancho del rectangulo en pixeles. Minimo 16.");
                    ImGui::InputInt("h##region", &rh);
                    ImGui::SetItemTooltip("Alto del rectangulo en pixeles. Minimo 16.");
                    if (ImGui::Button("Pin to this rect", ImVec2(-1, 0))) {
                        window_actions.region_x = rx;
                        window_actions.region_y = ry;
                        window_actions.region_w = std::max(rw, 16);
                        window_actions.region_h = std::max(rh, 16);
                        window_actions.region_attach_requested = true;
                    }
                    ImGui::SetItemTooltip("Fija la region. La entrada pasa a la app debajo (click-through).");
                }
            }

            // ---- Captures folder + recording ----
            if (ImGui::CollapsingHeader("Captures (screenshots + video)",
                                          ImGuiTreeNodeFlags_DefaultOpen)) {
                static char buf[512];
                if (buf[0] == 0 || std::string(buf) != capture_dir) {
                    std::snprintf(buf, sizeof(buf), "%s", capture_dir.c_str());
                }
                ImGui::TextDisabled("Folder where Ctrl+Alt+S / Ctrl+Alt+V save");
                ImGui::InputText("##capdir", buf, sizeof(buf));
                ImGui::SetItemTooltip("Carpeta donde se guardan screenshots y videos.\n"
                                      "Vacio = carpeta por defecto.");
                if (ImGui::Button("Browse...", ImVec2(110, 0))) {
                    std::string picked = browse_for_folder("Choose Tubelight capture folder");
                    if (!picked.empty()) {
                        capture_dir = picked;
                        std::snprintf(buf, sizeof(buf), "%s", picked.c_str());
                        capture_dir_changed = true;
                    }
                }
                ImGui::SetItemTooltip("Abre el dialogo de Windows para elegir carpeta.");
                ImGui::SameLine();
                if (ImGui::Button("Apply", ImVec2(80, 0))) {
                    capture_dir = buf;
                    capture_dir_changed = true;
                }
                ImGui::SetItemTooltip("Confirma el texto editado como nueva carpeta de capturas.");
                ImGui::SameLine();
                if (ImGui::Button("Default", ImVec2(90, 0))) {
                    capture_dir.clear();
                    std::snprintf(buf, sizeof(buf), "%s", "");
                    capture_dir_changed = true;
                }
                ImGui::SetItemTooltip("Restaura a %%USERPROFILE%%\\Pictures\\Tubelight.");

                ImGui::Separator();
                const char* kRecSources[] = {
                    "Overlay view (CRT-effect, what you see)",
                    "Full monitor (raw desktop, no effect)",
                    "Custom rect (raw desktop, no effect)",
                };
                if (ImGui::Combo("Record source", &sio.record_source, kRecSources, 3)) {
                    sio.record_changed = true;
                }
                ImGui::SetItemTooltip("De donde grabar el video:\n"
                                      "vista CRT (lo que ves), monitor completo, o rectangulo.");
                if (sio.record_source == 2) {
                    if (ImGui::InputInt("rec x", &sio.record_rect_x)) sio.record_changed = true;
                    ImGui::SetItemTooltip("X del rectangulo de grabacion (pixeles del monitor).");
                    if (ImGui::InputInt("rec y", &sio.record_rect_y)) sio.record_changed = true;
                    ImGui::SetItemTooltip("Y del rectangulo de grabacion.");
                    if (ImGui::InputInt("rec w", &sio.record_rect_w)) sio.record_changed = true;
                    ImGui::SetItemTooltip("Ancho del rectangulo de grabacion.");
                    if (ImGui::InputInt("rec h", &sio.record_rect_h)) sio.record_changed = true;
                    ImGui::SetItemTooltip("Alto del rectangulo de grabacion.");
                }
            }

            // ---- Mode checkboxes ----
            if (ImGui::CollapsingHeader("Behaviour", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Checkbox("Show status HUD", &hud_visible)) {
                    hud_changed = true;
                }
                ImGui::SetItemTooltip("HUD arriba a la derecha con perfil + modo + senal.\n"
                                      "Atajo: Ctrl+Alt+H");
                if (ImGui::Checkbox("Click-through (windowed)",
                                    &sio.clickthrough_user)) {
                    sio.clickthrough_changed = true;
                }
                ImGui::SetItemTooltip("El cursor atraviesa Tubelight; la app de abajo recibe clicks.\n"
                                      "Atajo: Ctrl+Alt+C");
                if (ImGui::Checkbox("Low-latency mode (vsync off)", &sio.low_latency)) {
                    sio.low_latency_changed = true;
                }
                ImGui::SetItemTooltip("Desactiva vsync. Menos retardo, posible tearing.\n"
                                      "Soft-cap 240 fps para no saturar GPU.");
                if (ImGui::Checkbox("Recordable by Snipping Tool / Game Bar / OBS",
                                    &sio.recordable)) {
                    sio.recordable_changed = true;
                }
                ImGui::SetItemTooltip("Cambia source a Magnification API con self-filter\n"
                                      "para que OBS / Game Bar puedan grabar Tubelight\n"
                                      "sin feedback infinito. Atajo: Ctrl+Alt+R");
                if (sio.recordable) {
                    ImGui::TextDisabled("Source via Magnification API w/ self-filter.");
                }
            }
            ImGui::EndTabItem();
        }

        // ====================== AUDIO TAB ======================
        if (ImGui::BeginTabItem("Audio")) {
            if (ImGui::Checkbox("Enable flyback whine (~15.7 kHz)", &audio_enabled)) {
                audio_changed = true;
            }
            ImGui::SetItemTooltip("Reproduce el chillido ~15.7 kHz del transformador flyback.\n"
                                  "Modulado por luminancia (mas blanco = mas fuerte).");
            if (ImGui::SliderFloat("Volume", &audio_volume, 0.0f, 1.0f, "%.2f")) {
                audio_changed = true;
            }
            ImGui::SetItemTooltip("Volumen del flyback. 0=silencio, 1=maximo. Default 0.20.");
            ImGui::TextDisabled("Off by default; persists in settings.json.");
            ImGui::TextDisabled("XAudio2 streaming voice in a worker thread.");
            ImGui::EndTabItem();
        }

        // ====================== HELP TAB ======================
        if (ImGui::BeginTabItem("Help")) {
            ImGui::TextDisabled("Tubelight v0.1.0-alpha");
            ImGui::TextDisabled("https://github.com/GS-RUN/tubelight");
            ImGui::Separator();
            ImGui::Text("Keyboard shortcuts (Ctrl+Alt + ...):");
            ImGui::BulletText("M  toggle menu");
            ImGui::BulletText("Q  quit");
            ImGui::BulletText("F  freeze captured frame");
            ImGui::BulletText("Enter  toggle fullscreen (keeps aspect)");
            ImGui::BulletText("T  attach to foreground window / detach");
            ImGui::BulletText("C  toggle click-through (windowed)");
            ImGui::BulletText("R  toggle recordable mode");
            ImGui::BulletText("H  toggle status HUD");
            ImGui::BulletText("S  PNG screenshot to capture folder");
            ImGui::BulletText("V  toggle MP4 video recording (needs ffmpeg)");
            ImGui::Separator();
            ImGui::TextDisabled("Pass debug (Ctrl+Alt + ...):");
            ImGui::BulletText("0  enable all 8 passes");
            ImGui::BulletText("1..8  toggle individual pass");
            ImGui::Separator();
            ImGui::TextDisabled("Recording with Win11 stock tools requires Ctrl+Alt+R");
            ImGui::TextDisabled("first. OBS Window Capture (WGC) works without it.");
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::Separator();
    if (ImGui::Button("Hide menu (Ctrl+Alt+M)", ImVec2(-1, 0))) {
        open_ = false;
    }
    ImGui::SetItemTooltip("Oculta el menu. Reabrir con Ctrl+Alt+M.");
    if (ImGui::Button("Quit overlay (Ctrl+Alt+Q)", ImVec2(-1, 0))) {
        want_quit = true;
    }
    ImGui::SetItemTooltip("Cierra Tubelight por completo. Atajo: Ctrl+Alt+Q");

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
    (void)sio;
#endif
}

void Menu::end_frame_to_screen() {
#ifdef TUBELIGHT_HAS_IMGUI
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif
}

} // namespace tubelight::overlay
