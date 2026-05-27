// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)

#include "io/shader_io.h"

#include <fstream>
#include <sstream>

namespace tubelight {

bool read_text_file(const std::string& path, std::string& out, std::string& error) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open()) {
        error = "failed to open " + path;
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

} // namespace tubelight
