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
};

// Reads settings.json if present. Missing fields keep their default values.
Settings load_settings();

// Writes settings to disk (best effort, errors logged to stderr).
void save_settings(const Settings& s);

// Returns the path of settings.json (creates parent dir on demand).
std::string settings_file_path();

} // namespace tubelight::overlay
