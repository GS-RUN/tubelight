// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN

#pragma once

#include <string>

namespace tubelight {

// Reads an entire text file into out. Returns false and fills error on failure.
bool read_text_file(const std::string& path, std::string& out, std::string& error);

} // namespace tubelight
