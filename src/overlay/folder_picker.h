// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)
//
// Opens the native folder-picker dialog. Used by the Captures section of
// the in-app menu so the user doesn't have to paste paths by hand.
//
// Returns the chosen folder on success or an empty string if the user
// cancelled / a platform-specific failure happened.

#pragma once

#include <string>

namespace tubelight::overlay {

std::string browse_for_folder(const std::string& title = "Choose capture folder");

} // namespace tubelight::overlay
