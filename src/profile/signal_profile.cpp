// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)

#include "profile/signal_profile.h"

namespace tubelight {

std::optional<Connection> parse_connection(std::string_view s, std::string& error_out) {
    if (s == "rf")        return Connection::RF;
    if (s == "composite") return Connection::Composite;
    if (s == "svideo")    return Connection::SVideo;
    if (s == "scart_rgb") return Connection::ScartRgb;
    if (s == "component") return Connection::Component;
    if (s == "rgb_vga")   return Connection::RgbVga;
    error_out = std::string("unknown connection: ") + std::string(s);
    return std::nullopt;
}

std::optional<Standard> parse_standard(std::string_view s, std::string& error_out) {
    if (s == "ntsc_m") return Standard::NtscM;
    if (s == "pal_bg") return Standard::PalBg;
    if (s == "pal_n")  return Standard::PalN;
    if (s == "secam")  return Standard::Secam;
    if (s == "none")   return Standard::None;
    error_out = std::string("unknown standard: ") + std::string(s);
    return std::nullopt;
}

std::optional<NoiseType> parse_noise_type(std::string_view s, std::string& error_out) {
    if (s == "pixel") return NoiseType::Pixel;
    if (s == "line")  return NoiseType::Line;
    if (s == "rf")    return NoiseType::Rf;
    if (s == "none")  return NoiseType::None;
    error_out = std::string("unknown noise_type: ") + std::string(s);
    return std::nullopt;
}

const char* connection_to_string(Connection c) {
    switch (c) {
        case Connection::RF:        return "rf";
        case Connection::Composite: return "composite";
        case Connection::SVideo:    return "svideo";
        case Connection::ScartRgb:  return "scart_rgb";
        case Connection::Component: return "component";
        case Connection::RgbVga:    return "rgb_vga";
    }
    return "unknown";
}

const char* standard_to_string(Standard s) {
    switch (s) {
        case Standard::NtscM: return "ntsc_m";
        case Standard::PalBg: return "pal_bg";
        case Standard::PalN:  return "pal_n";
        case Standard::Secam: return "secam";
        case Standard::None:  return "none";
    }
    return "unknown";
}

const char* noise_type_to_string(NoiseType n) {
    switch (n) {
        case NoiseType::Pixel: return "pixel";
        case NoiseType::Line:  return "line";
        case NoiseType::Rf:    return "rf";
        case NoiseType::None:  return "none";
    }
    return "unknown";
}

} // namespace tubelight
