// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)

#pragma once

#include "profile/crt_profile.h"  // for SourceCitation

#include <optional>
#include <string>
#include <string_view>

namespace tubelight {

enum class Connection {
    RF, Composite, SVideo, ScartRgb, Component, RgbVga
};

enum class Standard {
    NtscM, PalBg, PalN, Secam, None
};

enum class NoiseType {
    Pixel, Line, Rf, None
};

struct SignalProfile {
    std::string id;
    std::string display_name;
    Connection connection = Connection::Composite;
    Standard standard     = Standard::NtscM;

    double luma_mhz     = 4.2;
    double chroma_i_mhz = 0.5;
    double chroma_q_mhz = 0.5;
    SourceCitation bandwidth_source;

    double dot_crawl_strength     = 0.0;
    double rainbow_banding        = 0.0;
    double ringing_amount         = 0.0;
    double ghosting_offset_pixels = 0.0;
    NoiseType noise_type          = NoiseType::None;
    double noise_strength         = 0.0;

    double effective_tvl   = 350.0;
    double subcarrier_mhz  = 3.579545;
    double h_freq_khz      = 15.734;
};

std::optional<Connection> parse_connection(std::string_view s, std::string& error_out);
std::optional<Standard>   parse_standard  (std::string_view s, std::string& error_out);
std::optional<NoiseType>  parse_noise_type(std::string_view s, std::string& error_out);

const char* connection_to_string(Connection c);
const char* standard_to_string  (Standard s);
const char* noise_type_to_string(NoiseType n);

} // namespace tubelight
