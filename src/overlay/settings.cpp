// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)

#include "overlay/settings.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace tubelight::overlay {

namespace fs = std::filesystem;

namespace {

fs::path config_dir() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    fs::path base = appdata ? fs::path(appdata) : fs::current_path();
    return base / "Tubelight";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    fs::path base = xdg ? fs::path(xdg)
                  : home ? (fs::path(home) / ".config")
                  : fs::current_path();
    return base / "tubelight";
#endif
}

} // namespace

std::string settings_file_path() {
    fs::path dir = config_dir();
    std::error_code ec;
    fs::create_directories(dir, ec);
    return (dir / "settings.json").string();
}

Settings load_settings() {
    Settings s;
    std::ifstream in(settings_file_path());
    if (!in.is_open()) return s;
    try {
        nlohmann::json j;
        in >> j;
        if (j.contains("capture_dir") && j.at("capture_dir").is_string()) {
            s.capture_dir = j.at("capture_dir").get<std::string>();
        }
        if (j.contains("hud_visible") && j.at("hud_visible").is_boolean()) {
            s.hud_visible = j.at("hud_visible").get<bool>();
        }
        if (j.contains("crt_audio_enabled") && j.at("crt_audio_enabled").is_boolean()) {
            s.crt_audio_enabled = j.at("crt_audio_enabled").get<bool>();
        }
        if (j.contains("crt_audio_volume") && j.at("crt_audio_volume").is_number()) {
            s.crt_audio_volume = j.at("crt_audio_volume").get<float>();
        }
        if (j.contains("record_source") && j.at("record_source").is_number_integer()) {
            s.record_source = j.at("record_source").get<int>();
        }
        if (j.contains("record_rect") && j.at("record_rect").is_array() &&
            j.at("record_rect").size() == 4) {
            s.record_rect_x = j.at("record_rect")[0].get<int>();
            s.record_rect_y = j.at("record_rect")[1].get<int>();
            s.record_rect_w = j.at("record_rect")[2].get<int>();
            s.record_rect_h = j.at("record_rect")[3].get<int>();
        }
        if (j.contains("clickthrough_user") && j.at("clickthrough_user").is_boolean()) {
            s.clickthrough_user = j.at("clickthrough_user").get<bool>();
        }
        if (j.contains("low_latency") && j.at("low_latency").is_boolean()) {
            s.low_latency = j.at("low_latency").get<bool>();
        }
        if (j.contains("recordable") && j.at("recordable").is_boolean()) {
            s.recordable = j.at("recordable").get<bool>();
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[overlay] settings parse error: %s\n", e.what());
    }
    return s;
}

void save_settings(const Settings& s) {
    nlohmann::json j;
    j["capture_dir"]       = s.capture_dir;
    j["hud_visible"]       = s.hud_visible;
    j["crt_audio_enabled"] = s.crt_audio_enabled;
    j["crt_audio_volume"]  = s.crt_audio_volume;
    j["record_source"]     = s.record_source;
    j["record_rect"]       = { s.record_rect_x, s.record_rect_y,
                               s.record_rect_w, s.record_rect_h };
    j["clickthrough_user"] = s.clickthrough_user;
    j["low_latency"]       = s.low_latency;
    j["recordable"]        = s.recordable;
    std::ofstream out(settings_file_path());
    if (!out.is_open()) {
        std::fprintf(stderr, "[overlay] could not write %s\n", settings_file_path().c_str());
        return;
    }
    out << j.dump(2);
}

} // namespace tubelight::overlay
