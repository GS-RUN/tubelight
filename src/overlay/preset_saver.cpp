// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN

#include "overlay/preset_saver.h"
#include "profile/profile_loader.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace tubelight::overlay {

namespace fs = std::filesystem;

namespace {

const char* mask_type_to_str(int mt) {
    switch (mt) {
        case 1: return "shadow";
        case 2: return "aperture_grille";
        case 3: return "slot";
        case 4: return "diamond";
        case 5: return "cgwg";
        case 6: return "dot_trio";
        default: return "none";
    }
}

std::string aspect_float_to_str(float ar) {
    if (ar <= 0.0f) return "4:3";  // store something legible if fill mode
    if (std::abs(ar - 4.0f / 3.0f) < 0.01f)  return "4:3";
    if (std::abs(ar - 5.0f / 4.0f) < 0.01f)  return "5:4";
    if (std::abs(ar - 16.0f / 9.0f) < 0.01f) return "16:9";
    if (std::abs(ar - 16.0f / 10.0f) < 0.01f) return "16:10";
    if (std::abs(ar - 21.0f / 9.0f) < 0.01f) return "21:9";
    return "4:3";  // safe default
}

// Look up the on-disk path of <source_id>.json across the same search
// dirs that load_crt_profile_by_id uses. We need the raw bytes (and a
// known parent dir to fall back on), not the parsed struct.
std::string find_profile_path(const std::string& source_id) {
    for (const auto& dir : default_crt_profile_dirs()) {
        std::error_code ec;
        if (!fs::is_directory(dir, ec)) continue;
        fs::path candidate = fs::path(dir) / (source_id + ".json");
        if (fs::is_regular_file(candidate, ec)) {
            return candidate.string();
        }
    }
    return {};
}

// User-local profiles dir; created on demand.
fs::path user_crt_dir() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    fs::path base = appdata ? fs::path(appdata) : fs::current_path();
    return base / "Tubelight" / "profiles" / "crts";
#else
    const char* xdg  = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    fs::path base = xdg ? fs::path(xdg)
                  : home ? (fs::path(home) / ".config")
                  : fs::current_path();
    return base / "tubelight" / "profiles" / "crts";
#endif
}

} // namespace

bool save_crt_preset(const std::string& source_id,
                     const std::string& new_id,
                     const std::string& new_display_name,
                     const Pipeline::GlobalParams& p,
                     std::string& error_out) {
    if (new_id.empty()) {
        error_out = "preset id cannot be empty";
        return false;
    }
    // Reserve characters that would break paths.
    for (char c : new_id) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') {
            error_out = "preset id contains invalid filename character";
            return false;
        }
    }

    std::string base_path = find_profile_path(source_id);
    if (base_path.empty()) {
        error_out = "base profile '" + source_id + "' not found on disk";
        return false;
    }

    // Read + parse the base profile so we inherit its phosphor citation,
    // chromaticity coords, h/v frequencies etc. — fields the live pipeline
    // doesn't track but the schema validator requires.
    nlohmann::json j;
    try {
        std::ifstream in(base_path);
        if (!in.is_open()) {
            error_out = "could not open base profile " + base_path;
            return false;
        }
        in >> j;
    } catch (const std::exception& e) {
        error_out = std::string("base profile parse failed: ") + e.what();
        return false;
    }

    // Override the fields the user has actively tweaked. Fields we don't
    // store (phosphor.type, chromaticity, decay_ms, h/v freq) come along
    // from the base profile unchanged.
    j["id"]            = new_id;
    j["display_name"]  = new_display_name.empty() ? new_id : new_display_name;

    auto& tube = j["tube"];
    tube["mask_type"]     = mask_type_to_str(p.mask_type);
    tube["aspect_native"] = aspect_float_to_str(p.target_aspect);

    auto& beam = j["beam"];
    beam["scanline_strength"] = p.scanline_strength;

    auto& glass = j["glass"];
    glass["age"]  = p.glass_age;
    glass["tint"] = nlohmann::json::array({ p.glass_tint_r, p.glass_tint_g, p.glass_tint_b });

    // Mark provenance so the user can tell saved presets apart from bundled.
    j["era"] = "user-preset";
    if (tube.contains("source")) {
        tube["source"]["url"]          = "user-saved preset (Tubelight)";
        tube["source"]["retrieved_at"] = "user";
        tube["source"]["notes"]        = "Saved from current pipeline params; base: " + source_id;
    }

    // Write to user dir. Schema URL stays relative to the base profile's
    // location, which doesn't exist under user dir — replace with a
    // sentinel so the validator doesn't try to resolve it.
    fs::path dir = user_crt_dir();
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        error_out = "could not create user profile dir: " + ec.message();
        return false;
    }
    fs::path out_path = dir / (new_id + ".json");

    // Adjust $schema to be a relative path that resolves from user dir.
    // The user profile dir is at depth 4 (.../Tubelight/profiles/crts/);
    // the bundled schemas are alongside the bundled profiles. We use an
    // absolute "https://" sentinel so no validator on earth misresolves.
    j["$schema"] = "https://tubelight.local/schemas/crt_profile.schema.json";

    std::ofstream out(out_path);
    if (!out.is_open()) {
        error_out = "could not write " + out_path.string();
        return false;
    }
    out << j.dump(2);
    if (!out) {
        error_out = "write to " + out_path.string() + " failed mid-stream";
        return false;
    }
    return true;
}

} // namespace tubelight::overlay
