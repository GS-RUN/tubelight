// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// Entry point for the real overlay mode: a borderless always-on-top window
// that captures the desktop beneath it (via DXGI Desktop Duplication on
// Windows; PipeWire portal on Linux — v1.1) and applies Tubelight's 8-pass
// CRT pipeline in real time.
//
// CLI: `tubelight --overlay [--profile <id>] [--signal <id>]`

#pragma once

#include <string>

namespace tubelight::overlay {

struct Options {
    std::string profile_id;            // empty → use defaults
    std::string signal_id;             // empty → use defaults
    int  monitor_index = 0;            // which output to capture (Windows)
    bool fullscreen    = true;         // false → 16:9 windowed (v1.1)
};

// Runs the overlay until the user presses ESC. Returns CLI exit code:
//   0 normal exit
//   1 initialization failed (window / GL / capture)
//   2 unsupported platform (Linux today — PipeWire portal lands v1.1)
int run(const Options& opts);

} // namespace tubelight::overlay
