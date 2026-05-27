// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tubelight {

struct ImageData {
    std::vector<std::uint8_t> pixels; // tightly packed, row-major
    int width    = 0;
    int height   = 0;
    int channels = 0;
};

// Loads PNG/JPG/BMP/TGA via stb_image. Returns false and fills error on failure.
bool load_image(const std::string& path, ImageData& out, std::string& error,
                bool flip_vertical = true);

// Writes PNG via stb_image_write. Returns false and fills error on failure.
bool save_png(const std::string& path,
              const std::uint8_t* pixels,
              int width, int height, int channels,
              std::string& error,
              bool flip_vertical = false);

} // namespace tubelight
