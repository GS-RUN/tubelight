// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)

#include "overlay/menu.h"
#include "overlay/folder_picker.h"
#include "profile/profile_loader.h"
#include "profile/validator.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  #include <shellapi.h>
#endif

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
// Phase 4a.3: ImGui D3D12 renderer backend (Windows + DX12 build only).
#if defined(TUBELIGHT_HAVE_D3D12)
  #include <d3d12.h>
  #include <wrl/client.h>
  #if __has_include(<imgui_impl_dx12.h>)
    #include <imgui_impl_dx12.h>
    #include <imgui_impl_win32.h>
  #else
    #include <backends/imgui_impl_dx12.h>
    #include <backends/imgui_impl_win32.h>
  #endif
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

// Multi-accent dark theme inspired by professional audio plugins
// (FabFilter, Reaper) and modern dev tools â€” neutral dark base with a
// curated 6-colour accent system, each hue assigned to a functional
// category. Off-white text + soft 4-6 px rounding + generous padding.
// Tubelight's CRT look stays in the shader output; the chrome is its
// own design system, not part of the effect.
//
// Accent palette (kept harmonised â€” all desaturated, similar luminance,
// none of them shout):
//   accent_sky      â†’ primary / sliders / checks
//   accent_teal     â†’ Capture tab (system / camera vibe)
//   accent_amber    â†’ Image tab (warm visual params)
//   accent_coral    â†’ destructive (Quit button)
//   accent_lavender â†’ Help tab (informational)
//   accent_mint     â†’ Audio tab
// They co-exist on screen at the same moment but never cluster.
namespace pal {
    inline ImVec4 sky      (float a=1.0f) { return ImVec4(0.42f, 0.65f, 0.92f, a); }
    inline ImVec4 teal     (float a=1.0f) { return ImVec4(0.37f, 0.79f, 0.77f, a); }
    inline ImVec4 amber    (float a=1.0f) { return ImVec4(0.96f, 0.72f, 0.42f, a); }
    inline ImVec4 coral    (float a=1.0f) { return ImVec4(0.93f, 0.54f, 0.48f, a); }
    inline ImVec4 lavender (float a=1.0f) { return ImVec4(0.66f, 0.58f, 0.93f, a); }
    inline ImVec4 mint     (float a=1.0f) { return ImVec4(0.52f, 0.83f, 0.60f, a); }
}

void apply_tubelight_theme(ImGuiStyle& style) {
    auto& c = style.Colors;
    const ImVec4 accent      = pal::sky();
    const ImVec4 accent_soft = pal::sky(0.45f);

    // Text â€” off-white, slightly cool. Pure-white at long viewing
    // distances feels harsh; a few percent of grey helps.
    c[ImGuiCol_Text]                = ImVec4(0.86f, 0.87f, 0.89f, 1.00f);
    c[ImGuiCol_TextDisabled]        = ImVec4(0.50f, 0.52f, 0.56f, 1.00f);
    c[ImGuiCol_TextSelectedBg]      = ImVec4(0.42f, 0.65f, 0.92f, 0.35f);

    // Window backgrounds â€” three close shades for depth without high
    // contrast. Window slightly translucent so the CRT output stays
    // visible behind the menu (Tubelight is an overlay app â€” the
    // user should still see what they're tweaking).
    c[ImGuiCol_WindowBg]            = ImVec4(0.11f, 0.12f, 0.14f, 0.96f);
    c[ImGuiCol_ChildBg]             = ImVec4(0.11f, 0.12f, 0.14f, 0.00f);
    c[ImGuiCol_PopupBg]             = ImVec4(0.13f, 0.14f, 0.17f, 0.98f);

    // No harsh borders â€” depth comes from background tone differences.
    c[ImGuiCol_Border]              = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_BorderShadow]        = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Frame (inputs, sliders, combos) â€” elevated above window.
    c[ImGuiCol_FrameBg]             = ImVec4(0.18f, 0.19f, 0.22f, 1.00f);
    c[ImGuiCol_FrameBgHovered]      = ImVec4(0.22f, 0.24f, 0.28f, 1.00f);
    c[ImGuiCol_FrameBgActive]       = ImVec4(0.26f, 0.29f, 0.34f, 1.00f);

    // Title bar â€” slightly elevated from window background.
    c[ImGuiCol_TitleBg]             = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);
    c[ImGuiCol_TitleBgActive]       = ImVec4(0.16f, 0.18f, 0.21f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]    = ImVec4(0.10f, 0.11f, 0.13f, 0.80f);

    c[ImGuiCol_MenuBarBg]           = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);

    // Scrollbar â€” neutral until the user is actually dragging.
    c[ImGuiCol_ScrollbarBg]         = ImVec4(0.11f, 0.12f, 0.14f, 0.50f);
    c[ImGuiCol_ScrollbarGrab]       = ImVec4(0.35f, 0.38f, 0.43f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered]= ImVec4(0.42f, 0.46f, 0.52f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive] = accent;

    // Active-state pops use the accent.
    c[ImGuiCol_CheckMark]           = accent;
    c[ImGuiCol_SliderGrab]          = ImVec4(0.45f, 0.48f, 0.54f, 1.00f);
    c[ImGuiCol_SliderGrabActive]    = accent;

    // Buttons â€” match the frame palette so they don't shout.
    c[ImGuiCol_Button]              = ImVec4(0.21f, 0.23f, 0.27f, 1.00f);
    c[ImGuiCol_ButtonHovered]       = ImVec4(0.26f, 0.29f, 0.34f, 1.00f);
    c[ImGuiCol_ButtonActive]        = accent;

    // Collapsing headers â€” slightly more saturated than buttons so the
    // visual hierarchy reads correctly (header > button > frame).
    c[ImGuiCol_Header]              = ImVec4(0.18f, 0.20f, 0.24f, 1.00f);
    c[ImGuiCol_HeaderHovered]       = ImVec4(0.22f, 0.25f, 0.30f, 1.00f);
    c[ImGuiCol_HeaderActive]        = ImVec4(0.26f, 0.29f, 0.34f, 1.00f);

    // Separators â€” barely visible, just enough to group.
    c[ImGuiCol_Separator]           = ImVec4(0.20f, 0.22f, 0.26f, 1.00f);
    c[ImGuiCol_SeparatorHovered]    = accent_soft;
    c[ImGuiCol_SeparatorActive]     = accent;

    // Tabs
    c[ImGuiCol_Tab]                 = ImVec4(0.13f, 0.14f, 0.17f, 1.00f);
    c[ImGuiCol_TabHovered]          = ImVec4(0.22f, 0.25f, 0.30f, 1.00f);
    c[ImGuiCol_TabActive]           = ImVec4(0.18f, 0.20f, 0.24f, 1.00f);
    c[ImGuiCol_TabUnfocused]        = ImVec4(0.11f, 0.12f, 0.14f, 1.00f);
    c[ImGuiCol_TabUnfocusedActive]  = ImVec4(0.14f, 0.16f, 0.19f, 1.00f);

    // Resize grip + nav highlight + drag-drop target â€” all accent at
    // different alphas so the same colour signals "interactive".
    c[ImGuiCol_ResizeGrip]          = ImVec4(0.30f, 0.32f, 0.37f, 0.50f);
    c[ImGuiCol_ResizeGripHovered]   = accent_soft;
    c[ImGuiCol_ResizeGripActive]    = accent;
    c[ImGuiCol_DragDropTarget]      = accent;
    c[ImGuiCol_NavHighlight]        = accent;
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(0.42f, 0.65f, 0.92f, 0.70f);
    c[ImGuiCol_NavWindowingDimBg]   = ImVec4(0.05f, 0.05f, 0.05f, 0.35f);

    c[ImGuiCol_ModalWindowDimBg]    = ImVec4(0.05f, 0.05f, 0.05f, 0.40f);

    // Style â€” modern radius + comfortable padding.
    style.WindowRounding    = 6.0f;
    style.ChildRounding     = 4.0f;
    style.FrameRounding     = 4.0f;
    style.GrabRounding      = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.TabRounding       = 4.0f;
    style.PopupRounding     = 4.0f;

    style.WindowPadding     = ImVec2(14, 12);
    style.FramePadding      = ImVec2(8, 5);
    style.ItemSpacing       = ImVec2(10, 7);
    style.ItemInnerSpacing  = ImVec2(6, 5);
    style.CellPadding       = ImVec2(6, 4);

    style.WindowBorderSize  = 0.0f;
    style.ChildBorderSize   = 0.0f;
    style.FrameBorderSize   = 0.0f;
    style.TabBorderSize     = 0.0f;
    style.PopupBorderSize   = 0.0f;

    style.GrabMinSize       = 12.0f;
    style.ScrollbarSize     = 14.0f;

    style.WindowTitleAlign  = ImVec2(0.5f, 0.5f);
}

// RAII wrapper around BeginTabItem that pushes a category accent for
// the tab label colour AND for the CollapsingHeader hues inside the
// tab. Destructor handles EndTabItem + PopStyleColor cleanup so the
// callsite reads cleanly:
//     if (TintedTab t("Image", pal::amber(), pal::amber())) { ... }
class TintedTab {
public:
    TintedTab(const char* label, const ImVec4& label_col, const ImVec4& hdr_base) {
        ImGui::PushStyleColor(ImGuiCol_Text, label_col);
        selected_ = ImGui::BeginTabItem(label);
        ImGui::PopStyleColor();
        if (selected_) {
            ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(hdr_base.x, hdr_base.y, hdr_base.z, 0.18f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(hdr_base.x, hdr_base.y, hdr_base.z, 0.30f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(hdr_base.x, hdr_base.y, hdr_base.z, 0.42f));
        }
    }
    ~TintedTab() {
        if (selected_) {
            ImGui::PopStyleColor(3);
            ImGui::EndTabItem();
        }
    }
    explicit operator bool() const { return selected_; }
private:
    bool selected_ = false;
};

// Tooltip helper with a visually distinct presentation: slightly
// ---- i18n (Phase 4a follow-up): ES/EN menu language -------------------
// Autodetected from the OS UI language at init; toggled live by the EN/ES
// switch in the menu's top-right. T(en, es) returns the active-language
// string. (No persistence yet — autodetect each launch + per-session
// toggle.) The legacy strings were "Spanglish" (English widget labels +
// Spanish tooltips); T() makes both directions explicit.
enum class MenuLang { EN, ES };
MenuLang g_menu_lang = MenuLang::EN;
inline const char* T(const char* en, const char* es) {
    return g_menu_lang == MenuLang::ES ? es : en;
}
void detect_menu_language() {
#ifdef _WIN32
    if (PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_SPANISH)
        g_menu_lang = MenuLang::ES;
#endif
}

// lighter background than the menu (so the popup reads as "info"
// rather than blending into the chrome), a thin accent border, and
// generous padding. Called after each widget instead of
// ImGui::SetItemTooltip so we control the styling.
void tl_tooltip(const char* text) {
    // Tooltips deliberately use a WARM PARCHMENT palette that's a
    // different colour family from the rest of the UI (cool dark
    // grey + cool accents). Cream background with near-black text
    // and a deep gold border reads as a Post-it style annotation
    // pasted on top of the menu — instant visual separation from
    // the chrome, no ambiguity about "is this a control or info".
    //
    // Cream is a tone the rest of the UI never uses, so when a
    // tooltip pops up there's zero chance of mistaking it for an
    // interactive element.
    const ImVec4 cream  (0.96f, 0.91f, 0.74f, 0.98f); // bg
    const ImVec4 ink    (0.12f, 0.09f, 0.04f, 1.00f); // text
    const ImVec4 gold   (0.66f, 0.50f, 0.10f, 1.00f); // border
    ImGui::PushStyleColor(ImGuiCol_PopupBg, cream);
    ImGui::PushStyleColor(ImGuiCol_Border,  gold);
    ImGui::PushStyleColor(ImGuiCol_Text,    ink);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(12, 9));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   3.0f);
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(360.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);
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

#if defined(TUBELIGHT_HAS_IMGUI) && defined(TUBELIGHT_HAVE_D3D12)
// DX12 menu state: a small shader-visible SRV heap + bump allocator for
// ImGui's font/texture descriptors (1.92 allocates via callbacks).
namespace {
struct MenuDx12State {
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srv_heap;
    UINT desc_size = 0;
    UINT capacity  = 0;
    UINT next      = 0;
};
void menu_dx12_srv_alloc(ImGui_ImplDX12_InitInfo* info,
                         D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu,
                         D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu) {
    auto* st = static_cast<MenuDx12State*>(info->UserData);
    const UINT i = (st->next < st->capacity) ? st->next++ : 0;
    auto cpu = st->srv_heap->GetCPUDescriptorHandleForHeapStart();
    auto gpu = st->srv_heap->GetGPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(i) * st->desc_size;
    gpu.ptr += static_cast<UINT64>(i) * st->desc_size;
    *out_cpu = cpu;
    *out_gpu = gpu;
}
void menu_dx12_srv_free(ImGui_ImplDX12_InitInfo*, D3D12_CPU_DESCRIPTOR_HANDLE,
                        D3D12_GPU_DESCRIPTOR_HANDLE) {
    // Bump allocator — the menu allocates only a handful of descriptors
    // (font atlas, maybe re-created on DPI change); capacity 64 covers it.
}
} // namespace
#endif

bool Menu::init(GLFWwindow* window) {
#ifdef TUBELIGHT_HAS_IMGUI
    window_ = window;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    detect_menu_language();
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

bool Menu::init_dx12(void* hwnd, ID3D12Device* device,
                     ID3D12CommandQueue* queue, int num_frames_in_flight,
                     unsigned rtv_format) {
#if defined(TUBELIGHT_HAS_IMGUI) && defined(TUBELIGHT_HAVE_D3D12)
    window_    = nullptr;        // DX12 path uses a raw Win32 window, not GLFW
    dx12_mode_ = true;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    detect_menu_language();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    apply_tubelight_theme(ImGui::GetStyle());
    io.FontGlobalScale = 1.18f;

    // Win32 platform backend (the DX12 overlay window is a raw Win32 window
    // with WS_EX_NOREDIRECTIONBITMAP — created outside GLFW so DComp can
    // composite it AND clicks pass through). The overlay's WndProc forwards
    // messages to ImGui_ImplWin32_WndProcHandler.
    if (!ImGui_ImplWin32_Init(hwnd)) {
        std::fprintf(stderr, "[menu] ImGui_ImplWin32_Init failed\n");
        return false;
    }
    auto* st = new MenuDx12State();
    st->capacity = 64;
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = st->capacity;
    hd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&st->srv_heap)))) {
        std::fprintf(stderr, "[menu] dx12 SRV heap create failed\n");
        delete st;
        return false;
    }
    st->desc_size = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    dx12_state_ = st;

    ImGui_ImplDX12_InitInfo ii{};
    ii.Device               = device;
    ii.CommandQueue         = queue;
    ii.NumFramesInFlight    = num_frames_in_flight;
    ii.RTVFormat            = static_cast<DXGI_FORMAT>(rtv_format);
    ii.SrvDescriptorHeap    = st->srv_heap.Get();
    ii.SrvDescriptorAllocFn = menu_dx12_srv_alloc;
    ii.SrvDescriptorFreeFn  = menu_dx12_srv_free;
    ii.UserData             = st;
    if (!ImGui_ImplDX12_Init(&ii)) {
        std::fprintf(stderr, "[menu] ImGui_ImplDX12_Init failed\n");
        return false;
    }
    std::fprintf(stderr, "[menu] ImGui D3D12 backend ready\n");
    return true;
#else
    (void)window; (void)device; (void)queue; (void)num_frames_in_flight;
    (void)rtv_format;
    return false;
#endif
}

void Menu::shutdown() {
#ifdef TUBELIGHT_HAS_IMGUI
#if defined(TUBELIGHT_HAVE_D3D12)
    if (dx12_mode_) {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        delete static_cast<MenuDx12State*>(dx12_state_);
        dx12_state_ = nullptr;
        return;
    }
#endif
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

void Menu::begin_frame_dx12() {
#if defined(TUBELIGHT_HAS_IMGUI) && defined(TUBELIGHT_HAVE_D3D12)
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
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

    if (!ImGui::Begin("Tubelight",
                      &open_, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    auto& P = pipeline.params();
    const bool mono_locked = (P.monochrome == 1);

    // Language switch, top-right corner. Two small buttons; the active one
    // is highlighted. Autodetected at init, toggled live here.
    {
        const float right = ImGui::GetWindowContentRegionMax().x;
        ImGui::SameLine(right - 64.0f);
        const bool es = (g_menu_lang == MenuLang::ES);
        ImGui::PushStyleColor(ImGuiCol_Button,
                              es ? pal::amber() : ImVec4(0.18f,0.18f,0.20f,1.0f));
        if (ImGui::SmallButton("ES")) g_menu_lang = MenuLang::ES;
        ImGui::PopStyleColor();
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::PushStyleColor(ImGuiCol_Button,
                              !es ? pal::amber() : ImVec4(0.18f,0.18f,0.20f,1.0f));
        if (ImGui::SmallButton("EN")) g_menu_lang = MenuLang::EN;
        ImGui::PopStyleColor();
    }

    if (ImGui::BeginTabBar("##tubelight_tabs", ImGuiTabBarFlags_None)) {

        // ====================== PROFILE TAB ======================
        if (TintedTab _ttab{T("Profile","Perfil"), pal::sky(), pal::sky()}) {
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
            tl_tooltip("Perfil de tubo CRT: define mascara, fosforo, sombra y aspecto.\n"
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
                tl_tooltip("Perfil de senal: simula NTSC / PAL / RGB / composite.\n"
                                      "Bloqueado en monocromo (siempre RGB limpio).");
            } else {
                ImGui::TextDisabled("Signal: clean RGB (locked for monochrome)");
            }

            ImGui::Separator();
            ImGui::SliderFloat("Intensity x", &intensity_multiplier, 0.0f, 2.0f, "%.2f");
            tl_tooltip("Mezcla global con la imagen original.\n"
                                  "0=passthrough sin efecto, 1=normal, 2=maximo retro.\n"
                                  "Default: 1.0");

            ImGui::Separator();
            if (ImGui::TreeNode(T("Save current as preset...","Guardar como preset..."))) {
                static char id_buf[128];
                static char name_buf[256];
                ImGui::TextDisabled("Guarda en %%APPDATA%%\\Tubelight\\profiles\\crts\\<id>.json");
                ImGui::InputText("id (filename)",  id_buf,  sizeof(id_buf));
                tl_tooltip("Nombre de archivo (sin .json). Solo a-z, 0-9, guiones.\n"
                                      "Ej: mi_pvm_verde");
                ImGui::InputText("display name",   name_buf, sizeof(name_buf));
                tl_tooltip("Nombre visible en el combo CRT. Texto libre.\n"
                                      "Ej: Mi PVM verde");
                if (ImGui::Button("Save preset", ImVec2(-1, 0))) {
                    window_actions.preset_new_id        = id_buf;
                    window_actions.preset_display_name  = name_buf;
                    window_actions.save_preset_requested = true;
                    id_buf[0] = 0; name_buf[0] = 0;
                }
                tl_tooltip("Guarda los valores actuales como nuevo preset.");
                ImGui::TreePop();
            }
        }

        // ====================== IMAGE TAB ======================
        if (TintedTab _ttab{T("Image","Imagen"), pal::amber(), pal::amber()}) {
            if (mono_locked) {
                ImGui::TextDisabled("Monochrome tube: mask + per-channel persistence hidden.");
                ImGui::TextDisabled("(single-phosphor tubes have no mask). Tint + glow below.");
            }

            // ---- Scanlines / beam ----
            if (ImGui::CollapsingHeader(T("Scanlines / beam","Líneas / haz"), ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SliderFloat("Scanline strength", &P.scanline_strength, 0.0f, 1.0f, "%.2f");
                tl_tooltip("Cuan oscuras son las lineas entre escaneos.\n"
                                      "0=invisibles, 1=PVM agresivo. Default ~0.35.");
                ImGui::SliderFloat("Beam width",        &P.beam_width,        0.5f, 3.0f, "%.2f");
                tl_tooltip("Grosor del haz electronico.\n"
                                      "<1=fino y nitido, >2=grueso y borroso (TV vieja). Default 1.30.");
                ImGui::SliderFloat("CRT gamma",         &P.gamma_crt,         1.8f, 3.0f, "%.2f");
                tl_tooltip("Linealizacion de entrada.\n"
                                      "2.2=sRGB neutro, 2.5=mas contraste. Default 2.2.");
                ImGui::SliderFloat("Scanline count",    &P.scanline_count,    60.0f, 800.0f, "%.0f");
                tl_tooltip("Lineas visibles por frame.\n"
                                      "240=NTSC, 288=PAL, 350=terminal IBM, 480=VGA.");
            }

            // ---- Phosphor mask (colour CRT) ----
            if (!mono_locked && ImGui::CollapsingHeader(T("Phosphor mask (colour CRT)","Máscara de fósforo (CRT color)"))) {
                ImGui::Combo("Type", &P.mask_type, kMaskTypeLabels, 7);
                tl_tooltip("Geometria de fosforos:\n"
                                      "Shadow=triadico TV consumer, Aperture=Trinitron rayas verticales,\n"
                                      "Slot=mezcla (1084S), Diamond=PVM Sony.");
                ImGui::SliderFloat("Strength##mask",  &P.mask_strength,  0.0f, 1.0f, "%.2f");
                tl_tooltip("Cuan visible es la rejilla de subpixeles RGB.\n"
                                      "0=invisible, 1=cada subpixel separado. Default 0.22.");
                ImGui::SliderFloat("Pitch (px)", &P.mask_pitch_px, 1.0f, 10.0f, "%.1f");
                tl_tooltip("Tamano de la triada en pixeles de pantalla.\n"
                                      "3=denso, 8=PVM gigante. Default 3.0.");
            }

            // ---- Phosphor colour (monochrome) ----
            if (mono_locked && ImGui::CollapsingHeader(T("Phosphor colour (monochrome)","Color de fósforo (monocromo)"),
                                                        ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SliderFloat("R tint", &P.phosphor_color_r, 0.0f, 1.5f, "%.2f");
                tl_tooltip("Componente rojo del fosforo monocromo.\n"
                                      "0.05=P31 verde, 1.30=P3 ambar, 1.0=P4 blanco.");
                ImGui::SliderFloat("G tint", &P.phosphor_color_g, 0.0f, 1.5f, "%.2f");
                tl_tooltip("Componente verde. P31=1.30, P3=0.55, P4=1.00.");
                ImGui::SliderFloat("B tint", &P.phosphor_color_b, 0.0f, 1.5f, "%.2f");
                tl_tooltip("Componente azul. P31=0.18, P3=0.05, P4=1.10.");
                if (ImGui::Button("P31 green")) {
                    P.phosphor_color_r = 0.12f; P.phosphor_color_g = 1.30f; P.phosphor_color_b = 0.18f;
                }
                tl_tooltip("Verde puro de mainframe / Apple IIe / VT100.");
                ImGui::SameLine();
                if (ImGui::Button("P3 amber")) {
                    P.phosphor_color_r = 1.30f; P.phosphor_color_g = 0.55f; P.phosphor_color_b = 0.05f;
                }
                tl_tooltip("Ambar IBM 5151 / WYSE â€” calido para sesiones largas.");
                ImGui::SameLine();
                if (ImGui::Button("P1 oscope")) {
                    P.phosphor_color_r = 0.05f; P.phosphor_color_g = 1.35f; P.phosphor_color_b = 0.20f;
                }
                tl_tooltip("Verde-amarillo de osciloscopio Tektronix.");
                ImGui::SameLine();
                if (ImGui::Button("P4 white")) {
                    P.phosphor_color_r = 0.92f; P.phosphor_color_g = 1.00f; P.phosphor_color_b = 1.10f;
                }
                tl_tooltip("Blanco neutro: TV B&N + Mac Classic.");
                ImGui::SliderInt("Posterize levels", &P.posterize_levels, 0, 8);
                tl_tooltip("Cuantizacion de luminancia.\n"
                                      "0=continuo analogico, 2=1-bit Mac Classic, 4-6=terminal de texto.");
            }

            // ---- Bloom / glow ----
            if (!mono_locked && ImGui::CollapsingHeader(T("Bloom / halation","Bloom / halación"))) {
                ImGui::SliderFloat("Bloom##color",    &P.bloom_strength,    0.0f, 1.0f, "%.2f");
                tl_tooltip("Sangrado de zonas brillantes a oscuras\n"
                                      "(sobreexcitacion del haz electronico).");
                ImGui::SliderFloat("Halation", &P.halation_strength, 0.0f, 1.0f, "%.2f");
                tl_tooltip("Aro rojizo alrededor de blancos\n"
                                      "(luz rebotando dentro del vidrio del tubo).");
            }
            if (mono_locked && ImGui::CollapsingHeader(T("Phosphor glow","Brillo de fósforo"))) {
                ImGui::SliderFloat("Bloom##glow",    &P.bloom_strength,    0.0f, 1.0f, "%.2f");
                tl_tooltip("Bloom del fosforo. Mono tubes solo bloom, no halacion.");
            }

            // ---- Persistence ----
            if (!mono_locked && ImGui::CollapsingHeader(T("Phosphor persistence (per-channel)","Persistencia de fósforo (por canal)"))) {
                ImGui::SliderFloat("Strength##pers_c",  &P.persistence_strength, 0.0f, 0.95f, "%.2f");
                tl_tooltip("Cuanto rastro deja el fosforo entre frames.\n"
                                      "0=sin rastro, 0.95=largo (osciloscopio).");
                ImGui::SliderFloat("R ratio",   &P.persistence_ratio_r,  0.0f, 1.0f, "%.2f");
                tl_tooltip("Decaimiento relativo del rojo. P22 colour: ~1.0 (rastro calido).");
                ImGui::SliderFloat("G ratio",   &P.persistence_ratio_g,  0.0f, 1.0f, "%.2f");
                tl_tooltip("Decaimiento relativo del verde. P22 tipico: 0.5.");
                ImGui::SliderFloat("B ratio",   &P.persistence_ratio_b,  0.0f, 1.0f, "%.2f");
                tl_tooltip("Decaimiento relativo del azul. P22 tipico: 0.5.");
                ImGui::TextDisabled("P22 colour CRT: R~1.0, G~0.5, B~0.5 (warm trail)");
            }
            if (mono_locked && ImGui::CollapsingHeader(T("Phosphor persistence (afterglow)","Persistencia de fósforo (afterglow)"))) {
                ImGui::SliderFloat("Strength##pers_m", &P.persistence_strength, 0.0f, 0.95f, "%.2f");
                tl_tooltip("Persistencia del fosforo monocromo.\n"
                                      "P31/P4 cortos (~0). P3 medio (~0.3). P1 largo (~0.7).");
            }

            // ---- Composition ----
            if (ImGui::CollapsingHeader(T("Composition","Composición"))) {
                if (!mono_locked) {
                    ImGui::SliderFloat("Barrel",        &P.barrel_strength,   0.0f, 0.20f, "%.3f");
                    tl_tooltip("Distorsion de barril (curvatura del tubo).\n"
                                          "0=plano, 0.05=PVM, 0.20=TV redondeada.");
                    ImGui::SliderFloat("Vignette",      &P.vignette_strength, 0.0f, 1.0f, "%.2f");
                    tl_tooltip("Oscurecimiento de las esquinas.");
                    ImGui::SliderFloat("Display gamma", &P.gamma_display,     1.8f, 3.0f, "%.2f");
                    tl_tooltip("Gamma final hacia tu monitor. 2.2=sRGB. Default 2.2.");
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
                tl_tooltip("Forma del area de imagen.\n"
                                      "Fill=sin barras, 4:3=TV vintage, 16:9=moderno.\n"
                                      "Default viene del perfil CRT (aspect_native).");

                if (ImGui::Button("Snap window to aspect", ImVec2(180, 0))) {
                    if (P.target_aspect > 0.0f) {
                        window_actions.snap_to_aspect_requested = true;
                    }
                }
                tl_tooltip("Redimensiona la ventana de Tubelight\n"
                                      "para que la imagen llene sin barras.");
                ImGui::SameLine();
                const char* fs_label = window_actions.is_fullscreen
                                        ? "Exit fullscreen"
                                        : "Go fullscreen";
                if (ImGui::Button(fs_label, ImVec2(-1, 0))) {
                    window_actions.toggle_fullscreen_requested = true;
                }
                tl_tooltip("Pantalla completa borderless en el monitor actual.\n"
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
                tl_tooltip("Marco dibujado en las barras laterales.\n"
                                      "Default viene del perfil CRT.");
            }

            // ---- Pass toggles ----
            if (ImGui::CollapsingHeader(T("Pass toggles (advanced)","Pasadas (avanzado)"))) {
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
                    tl_tooltip(kPassTooltips[i]);
                }
            }
        }

        // ====================== CAPTURE TAB ======================
        if (TintedTab _ttab{T("Capture","Captura"), pal::teal(), pal::teal()}) {
            // ---- Target window ----
            if (ImGui::CollapsingHeader(T("Target window","Ventana objetivo"))) {
                if (window_actions.is_tracking_target) {
                    ImGui::Text("Tracking: %s",
                                window_actions.target_title.empty()
                                    ? "(unknown title)"
                                    : window_actions.target_title.c_str());
                    ImGui::TextDisabled("Tubelight follows this window with click-through.");
                    if (ImGui::Button("Detach", ImVec2(-1, 0))) {
                        window_actions.detach_target_requested = true;
                    }
                    tl_tooltip("Suelta la ventana seguida. Atajo: Ctrl+Alt+T");
                } else {
                    static char title_buf[256];
                    ImGui::InputText("##targettitle", title_buf, sizeof(title_buf));
                    tl_tooltip("Substring del titulo a seguir (case-insensitive).\n"
                                          "Ej: notepad");
                    if (ImGui::Button("Track by title", ImVec2(150, 0))) {
                        window_actions.title_to_track = title_buf;
                        window_actions.track_by_title_requested = true;
                    }
                    tl_tooltip("Engancha a la ventana cuyo titulo contiene el texto.");
                    ImGui::SameLine();
                    if (ImGui::Button("Track foreground", ImVec2(-1, 0))) {
                        window_actions.track_foreground_requested = true;
                    }
                    tl_tooltip("Engancha a la ventana que tenia foco antes del menu.\n"
                                          "Atajo: Ctrl+Alt+T");
                }
            }

            // ---- Region ----
            if (ImGui::CollapsingHeader(T("Region (fixed rect)","Región (rect fijo)"))) {
                if (window_actions.is_region_active) {
                    ImGui::TextDisabled("Pinned to a fixed monitor-relative rect.");
                    if (ImGui::Button("Detach region", ImVec2(-1, 0))) {
                        window_actions.region_detach_requested = true;
                    }
                    tl_tooltip("Suelta el rectangulo fijo.");
                } else {
                    static int rx = 100, ry = 100, rw = 800, rh = 600;
                    ImGui::InputInt("x##region", &rx);
                    tl_tooltip("Coordenada X en pixeles del monitor (0=borde izquierdo).");
                    ImGui::InputInt("y##region", &ry);
                    tl_tooltip("Coordenada Y en pixeles del monitor (0=borde superior).");
                    ImGui::InputInt("w##region", &rw);
                    tl_tooltip("Ancho del rectangulo en pixeles. Minimo 16.");
                    ImGui::InputInt("h##region", &rh);
                    tl_tooltip("Alto del rectangulo en pixeles. Minimo 16.");
                    if (ImGui::Button("Pin to this rect", ImVec2(-1, 0))) {
                        window_actions.region_x = rx;
                        window_actions.region_y = ry;
                        window_actions.region_w = std::max(rw, 16);
                        window_actions.region_h = std::max(rh, 16);
                        window_actions.region_attach_requested = true;
                    }
                    tl_tooltip("Fija la region. La entrada pasa a la app debajo (click-through).");
                }
            }

            // ---- Captures folder + recording ----
            if (ImGui::CollapsingHeader(T("Captures (screenshots + video)","Capturas (foto + vídeo)"),
                                          ImGuiTreeNodeFlags_DefaultOpen)) {
                static char buf[512];
                if (buf[0] == 0 || std::string(buf) != capture_dir) {
                    std::snprintf(buf, sizeof(buf), "%s", capture_dir.c_str());
                }
                ImGui::TextDisabled("Folder where Ctrl+Alt+S / Ctrl+Alt+V save");
                ImGui::InputText("##capdir", buf, sizeof(buf));
                tl_tooltip("Carpeta donde se guardan screenshots y videos.\n"
                                      "Vacio = carpeta por defecto.");
                if (ImGui::Button("Browse...", ImVec2(110, 0))) {
                    std::string picked = browse_for_folder("Choose Tubelight capture folder");
                    if (!picked.empty()) {
                        capture_dir = picked;
                        std::snprintf(buf, sizeof(buf), "%s", picked.c_str());
                        capture_dir_changed = true;
                    }
                }
                tl_tooltip("Abre el dialogo de Windows para elegir carpeta.");
                ImGui::SameLine();
                if (ImGui::Button("Apply", ImVec2(80, 0))) {
                    capture_dir = buf;
                    capture_dir_changed = true;
                }
                tl_tooltip("Confirma el texto editado como nueva carpeta de capturas.");
                ImGui::SameLine();
                if (ImGui::Button("Default", ImVec2(90, 0))) {
                    capture_dir.clear();
                    std::snprintf(buf, sizeof(buf), "%s", "");
                    capture_dir_changed = true;
                }
                tl_tooltip("Restaura a %%USERPROFILE%%\\Pictures\\Tubelight.");

                ImGui::Separator();
                const char* kRecSources[] = {
                    "Overlay view (CRT-effect, what you see)",
                    "Full monitor (raw desktop, no effect)",
                    "Custom rect (raw desktop, no effect)",
                };
                if (ImGui::Combo("Record source", &sio.record_source, kRecSources, 3)) {
                    sio.record_changed = true;
                }
                tl_tooltip("De donde grabar el video:\n"
                                      "vista CRT (lo que ves), monitor completo, o rectangulo.");
                if (sio.record_source == 2) {
                    if (ImGui::InputInt("rec x", &sio.record_rect_x)) sio.record_changed = true;
                    tl_tooltip("X del rectangulo de grabacion (pixeles del monitor).");
                    if (ImGui::InputInt("rec y", &sio.record_rect_y)) sio.record_changed = true;
                    tl_tooltip("Y del rectangulo de grabacion.");
                    if (ImGui::InputInt("rec w", &sio.record_rect_w)) sio.record_changed = true;
                    tl_tooltip("Ancho del rectangulo de grabacion.");
                    if (ImGui::InputInt("rec h", &sio.record_rect_h)) sio.record_changed = true;
                    tl_tooltip("Alto del rectangulo de grabacion.");
                }
            }

            // ---- Mode checkboxes ----
            if (ImGui::CollapsingHeader(T("Behaviour","Comportamiento"), ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Checkbox("Show status HUD", &hud_visible)) {
                    hud_changed = true;
                }
                tl_tooltip("HUD arriba a la derecha con perfil + modo + senal.\n"
                                      "Atajo: Ctrl+Alt+H");
                // ADR-0001 §2: click-through is no longer a user toggle.
                // Each overlay mode has its own click-through policy
                // (windowed = off, target/region/fullscreen = on).
                ImGui::TextDisabled("Click-through: por modo (windowed=off; target/region/fullscreen=on)");
                if (ImGui::Checkbox("Low-latency mode (vsync off)", &sio.low_latency)) {
                    sio.low_latency_changed = true;
                }
                tl_tooltip("Desactiva vsync. Menos retardo, posible tearing.\n"
                                      "Soft-cap 240 fps para no saturar GPU.");
                // ADR-0001 §1: recordable is always-on. Snipping Tool /
                // Game Bar / OBS see the overlay automatically.
                ImGui::TextDisabled("Recordable: siempre on (Snipping Tool / Game Bar / OBS lo ven)");
                tl_tooltip("Cambia source a Magnification API con self-filter\n"
                                      "para que OBS / Game Bar puedan grabar Tubelight\n"
                                      "sin feedback infinito. Atajo: Ctrl+Alt+R");
                if (sio.recordable) {
                    ImGui::TextDisabled("Source via Magnification API w/ self-filter.");
                }
            }
        }

        // ====================== AUDIO TAB ======================
        if (TintedTab _ttab{"Audio", pal::mint(), pal::mint()}) {
            if (ImGui::Checkbox("Enable flyback whine (~15.7 kHz)", &audio_enabled)) {
                audio_changed = true;
            }
            tl_tooltip("Reproduce el chillido ~15.7 kHz del transformador flyback.\n"
                                  "Modulado por luminancia (mas blanco = mas fuerte).");
            if (ImGui::SliderFloat("Volume", &audio_volume, 0.0f, 1.0f, "%.2f")) {
                audio_changed = true;
            }
            tl_tooltip("Volumen del flyback. 0=silencio, 1=maximo. Default 0.20.");
            ImGui::TextDisabled("Off by default; persists in settings.json.");
            ImGui::TextDisabled("XAudio2 streaming voice in a worker thread.");
        }

        // ====================== HELP TAB ======================
        if (TintedTab _ttab{T("Help","Ayuda"), pal::lavender(), pal::lavender()}) {
            ImGui::TextDisabled("Tubelight v0.1.6");
            ImGui::TextDisabled("https://github.com/GS-RUN/tubelight");

            // -- Open user manual ---------------------------------------
            // The single-file HTML lives in docs/manual/manual.html
            // relative to the binary. Resolve once and open with the
            // OS shell so the user's default browser handles it.
            ImGui::PushStyleColor(ImGuiCol_Button,        pal::mint(0.30f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, pal::mint(0.55f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  pal::mint(0.85f));
            if (ImGui::Button("Open user manual (manual.html)", ImVec2(-1, 0))) {
#ifdef _WIN32
                std::error_code ec;
                fs::path exe_dir;
                wchar_t buf[MAX_PATH] = {};
                if (GetModuleFileNameW(nullptr, buf, MAX_PATH) > 0) {
                    exe_dir = fs::path(buf).parent_path();
                } else {
                    exe_dir = fs::current_path(ec);
                }
                // Search common layouts: dev tree, release zip, sibling.
                const fs::path candidates[] = {
                    exe_dir / "docs" / "manual" / "manual.html",
                    exe_dir.parent_path() / "docs" / "manual" / "manual.html",
                    exe_dir.parent_path().parent_path() / "docs" / "manual" / "manual.html",
                    exe_dir / "manual" / "manual.html",
                    exe_dir / "manual.html",
                };
                fs::path target;
                for (const auto& p : candidates) {
                    if (fs::exists(p, ec)) { target = p; break; }
                }
                if (!target.empty()) {
                    ShellExecuteW(nullptr, L"open", target.wstring().c_str(),
                                  nullptr, nullptr, SW_SHOWNORMAL);
                } else {
                    // Fallback to the public GitHub release page.
                    ShellExecuteW(nullptr, L"open",
                                  L"https://github.com/GS-RUN/tubelight",
                                  nullptr, nullptr, SW_SHOWNORMAL);
                }
#endif
            }
            ImGui::PopStyleColor(3);
            tl_tooltip("Abre el manual de usuario en HTML en tu navegador por defecto. Si no encuentra el archivo local, abre el repo en GitHub.");

            ImGui::Separator();
            ImGui::Text("Keyboard shortcuts (Ctrl+Alt + ...):");
            ImGui::BulletText("M  toggle menu");
            ImGui::BulletText("Q  quit");
            ImGui::BulletText("F  freeze captured frame");
            ImGui::BulletText("Enter  toggle fullscreen (keeps aspect)");
            ImGui::BulletText("T  attach to foreground window / detach");
            ImGui::BulletText("H  toggle status HUD");
            ImGui::BulletText("S  PNG screenshot to capture folder");
            ImGui::BulletText("V  toggle MP4 video recording (needs ffmpeg)");
            ImGui::Separator();
            ImGui::TextDisabled("Pass debug (Ctrl+Alt + ...):");
            ImGui::BulletText("0  enable all 8 passes");
            ImGui::BulletText("1..8  toggle individual pass");
            ImGui::Separator();
            ImGui::TextDisabled("Recordable + click-through son ahora always-on");
            ImGui::TextDisabled("por modo (ADR-0001). Sin atajos R ni C.");
        }

        ImGui::EndTabBar();
    }

    ImGui::Separator();
    if (ImGui::Button("Hide menu (Ctrl+Alt+M)", ImVec2(-1, 0))) {
        open_ = false;
    }
    tl_tooltip("Oculta el menu. Reabrir con Ctrl+Alt+M.");
    // Quit button uses coral accent — destructive action, deserves
    // its own colour so the user notices what they're clicking.
    ImGui::PushStyleColor(ImGuiCol_Button,        pal::coral(0.30f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, pal::coral(0.55f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  pal::coral(0.85f));
    if (ImGui::Button("Quit overlay (Ctrl+Alt+Q)", ImVec2(-1, 0))) {
        want_quit = true;
    }
    ImGui::PopStyleColor(3);
    tl_tooltip("Cierra Tubelight por completo. Atajo: Ctrl+Alt+Q");

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

void Menu::end_frame_to_screen_dx12(ID3D12GraphicsCommandList* cmd_list) {
#if defined(TUBELIGHT_HAS_IMGUI) && defined(TUBELIGHT_HAVE_D3D12)
    ImGui::Render();
    auto* st = static_cast<MenuDx12State*>(dx12_state_);
    if (st && cmd_list) {
        ID3D12DescriptorHeap* heaps[] = { st->srv_heap.Get() };
        cmd_list->SetDescriptorHeaps(1, heaps);
        ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmd_list);
    }
#else
    (void)cmd_list;
#endif
}

} // namespace tubelight::overlay
