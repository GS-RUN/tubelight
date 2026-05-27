// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)

#pragma once

#include "profile/crt_profile.h"
#include "profile/signal_profile.h"

#include <string>
#include <variant>
#include <vector>

namespace tubelight {

enum class ProfileKind {
    Unknown,
    Crt,
    Signal
};

struct ValidationResult {
    bool ok = false;
    ProfileKind kind = ProfileKind::Unknown;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

// Reads a profile JSON, auto-detects its kind, validates against the schema
// invariants in CONTRACTS.md. Populates result.errors / warnings.
// On success, the parsed profile is returned via the out variant.
struct ParsedProfile {
    ProfileKind kind = ProfileKind::Unknown;
    std::variant<std::monostate, CRTProfile, SignalProfile> data;
};

ValidationResult validate_profile_file(const std::string& path);
ValidationResult validate_profile_file(const std::string& path, ParsedProfile& parsed_out);

// Pretty-print result to stdout/stderr in the same format used by
// `tubelight --validate-profile`. Returns a CLI exit code (0=ok, 3=invalid).
int print_validation_result(const std::string& path, const ValidationResult& r);

} // namespace tubelight
