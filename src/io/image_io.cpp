// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN

#include "io/image_io.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace tubelight {

bool load_image(const std::string& path, ImageData& out, std::string& error,
                bool flip_vertical) {
    stbi_set_flip_vertically_on_load(flip_vertical ? 1 : 0);
    int w = 0, h = 0, c = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &c, 0);
    if (!data) {
        error = std::string("stb_image: ") + (stbi_failure_reason() ? stbi_failure_reason() : "unknown error")
              + " (" + path + ")";
        return false;
    }
    out.width = w;
    out.height = h;
    out.channels = c;
    const size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * static_cast<size_t>(c);
    out.pixels.assign(data, data + bytes);
    stbi_image_free(data);
    return true;
}

bool save_png(const std::string& path,
              const std::uint8_t* pixels,
              int width, int height, int channels,
              std::string& error,
              bool flip_vertical) {
    stbi_flip_vertically_on_write(flip_vertical ? 1 : 0);
    int ok = stbi_write_png(path.c_str(), width, height, channels, pixels, width * channels);
    if (!ok) {
        error = "stbi_write_png failed for " + path;
        return false;
    }
    return true;
}

} // namespace tubelight
