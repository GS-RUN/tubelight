// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)

#pragma once

#include "profile/crt_profile.h"
#include "profile/signal_profile.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tubelight {

// Returns absolute paths to the bundled profile directories shipped with the
// repo (`<source>/profiles/crts`, `<source>/profiles/signals`).
std::vector<std::string> default_crt_profile_dirs();
std::vector<std::string> default_signal_profile_dirs();

// Loads + validates a CRT profile by id from the given dirs (or default dirs
// if empty). Returns nullopt on not-found / invalid.
std::optional<CRTProfile> load_crt_profile_by_id(std::string_view id,
                                                  std::string& error_out,
                                                  const std::vector<std::string>& search_dirs = {});

std::optional<SignalProfile> load_signal_profile_by_id(std::string_view id,
                                                        std::string& error_out,
                                                        const std::vector<std::string>& search_dirs = {});

// Loads + validates a profile from an explicit file path.
std::optional<CRTProfile>    load_crt_profile_from_file   (const std::string& path, std::string& error_out);
std::optional<SignalProfile> load_signal_profile_from_file(const std::string& path, std::string& error_out);

} // namespace tubelight
