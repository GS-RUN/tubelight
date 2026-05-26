// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN

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
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[overlay] settings parse error: %s\n", e.what());
    }
    return s;
}

void save_settings(const Settings& s) {
    nlohmann::json j;
    j["capture_dir"] = s.capture_dir;
    std::ofstream out(settings_file_path());
    if (!out.is_open()) {
        std::fprintf(stderr, "[overlay] could not write %s\n", settings_file_path().c_str());
        return;
    }
    out << j.dump(2);
}

} // namespace tubelight::overlay
