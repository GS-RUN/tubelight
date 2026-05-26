// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN

#include "profile/profile_loader.h"
#include "profile/validator.h"

#include <filesystem>
#include <variant>

namespace tubelight {

namespace fs = std::filesystem;

namespace {

// Resolved at build time relative to the repository root.
#ifndef TUBELIGHT_PROFILE_DIR
#  ifdef TUBELIGHT_SHADER_DIR
#    define TUBELIGHT_PROFILE_DIR TUBELIGHT_SHADER_DIR "/../profiles"
#  else
#    define TUBELIGHT_PROFILE_DIR "profiles"
#  endif
#endif

std::vector<std::string> default_dirs_for_subdir(const char* subdir) {
    std::vector<std::string> dirs;

    // Bundled (in source tree).
    fs::path bundled = fs::path(TUBELIGHT_PROFILE_DIR) / subdir;
    dirs.emplace_back(bundled.lexically_normal().string());

    // User-local config.
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    if (appdata) {
        dirs.emplace_back((fs::path(appdata) / "Tubelight" / "profiles" / subdir).string());
    }
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    if (xdg) {
        dirs.emplace_back((fs::path(xdg) / "tubelight" / "profiles" / subdir).string());
    } else if (home) {
        dirs.emplace_back((fs::path(home) / ".config" / "tubelight" / "profiles" / subdir).string());
    }
#endif

    return dirs;
}

template <typename T>
std::optional<T> load_by_id_in_dirs(std::string_view id,
                                    const std::vector<std::string>& dirs,
                                    ProfileKind expected_kind,
                                    std::string& error_out) {
    for (const auto& dir : dirs) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".json") continue;

            // Cheap match first: filename stem == id
            if (entry.path().stem().string() != std::string(id)) continue;

            ParsedProfile parsed;
            auto vr = validate_profile_file(entry.path().string(), parsed);
            if (!vr.ok) {
                error_out = "profile " + entry.path().string() + " failed validation";
                continue;
            }
            if (parsed.kind != expected_kind) {
                continue;
            }
            return std::get<T>(parsed.data);
        }
    }
    error_out = "profile id '" + std::string(id) + "' not found";
    return std::nullopt;
}

template <typename T>
std::optional<T> load_from_file_typed(const std::string& path,
                                      ProfileKind expected_kind,
                                      std::string& error_out) {
    ParsedProfile parsed;
    auto vr = validate_profile_file(path, parsed);
    if (!vr.ok) {
        error_out = path + " failed validation";
        return std::nullopt;
    }
    if (parsed.kind != expected_kind) {
        error_out = path + " is not the expected profile kind";
        return std::nullopt;
    }
    return std::get<T>(parsed.data);
}

} // namespace

std::vector<std::string> default_crt_profile_dirs() {
    return default_dirs_for_subdir("crts");
}

std::vector<std::string> default_signal_profile_dirs() {
    return default_dirs_for_subdir("signals");
}

std::optional<CRTProfile> load_crt_profile_by_id(std::string_view id,
                                                  std::string& error_out,
                                                  const std::vector<std::string>& search_dirs) {
    const auto dirs = search_dirs.empty() ? default_crt_profile_dirs() : search_dirs;
    return load_by_id_in_dirs<CRTProfile>(id, dirs, ProfileKind::Crt, error_out);
}

std::optional<SignalProfile> load_signal_profile_by_id(std::string_view id,
                                                        std::string& error_out,
                                                        const std::vector<std::string>& search_dirs) {
    const auto dirs = search_dirs.empty() ? default_signal_profile_dirs() : search_dirs;
    return load_by_id_in_dirs<SignalProfile>(id, dirs, ProfileKind::Signal, error_out);
}

std::optional<CRTProfile> load_crt_profile_from_file(const std::string& path, std::string& error_out) {
    return load_from_file_typed<CRTProfile>(path, ProfileKind::Crt, error_out);
}

std::optional<SignalProfile> load_signal_profile_from_file(const std::string& path, std::string& error_out) {
    return load_from_file_typed<SignalProfile>(path, ProfileKind::Signal, error_out);
}

} // namespace tubelight
