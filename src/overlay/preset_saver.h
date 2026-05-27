// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)
//
// Serialises the user's current pipeline params as a new CRT profile JSON
// in the user-local config directory (%APPDATA%\Tubelight\profiles\crts or
// ~/.config/tubelight/profiles/crts). The implementation loads the base
// profile (source_id) from disk as JSON, overrides the user-tunable
// fields, rewrites id + display_name, and writes the result so the rest
// of the loader machinery picks it up just like a bundled profile.

#pragma once

#include "core/pipeline.h"

#include <string>

namespace tubelight::overlay {

// Returns true on success. `error_out` populated on failure. The new
// preset will be loadable by `new_id` after a profile-list refresh.
bool save_crt_preset(const std::string& source_id,
                     const std::string& new_id,
                     const std::string& new_display_name,
                     const Pipeline::GlobalParams& params,
                     std::string& error_out);

} // namespace tubelight::overlay
