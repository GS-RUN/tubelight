// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN

#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace tubelight {

enum class MaskType {
    None,
    Shadow,
    ApertureGrille,
    Slot,
    Diamond,
    CgwgMix,
    DotTrio
};

enum class PhosphorType {
    P1, P3, P4, P22, P31, Custom
};

enum class ScreenCurvature {
    Flat, Mild, Aggressive
};

enum class IntensityCurve {
    Linear, Gauss, Sigmoid
};

enum class InterlaceMode {
    Off, TwoToOne, Rf
};

struct SourceCitation {
    std::string url;
    std::string retrieved_at;
    std::string notes;
};

struct CRTProfile {
    std::string id;
    std::string display_name;
    std::string era;

    // tube
    MaskType mask_type = MaskType::Shadow;
    std::optional<double> dot_pitch_mm;     // null for monochrome / NEEDS-MEASUREMENT
    ScreenCurvature screen_curvature = ScreenCurvature::Flat;
    double diagonal_inches = 14.0;
    std::string aspect_native = "4:3";
    SourceCitation tube_source;

    // phosphor
    PhosphorType phosphor_type = PhosphorType::P22;
    double decay_ms_r = 1.0;
    double decay_ms_g = 0.08;
    double decay_ms_b = 0.08;
    std::optional<std::array<double, 2>> chromaticity_r;
    std::optional<std::array<double, 2>> chromaticity_g;
    std::optional<std::array<double, 2>> chromaticity_b;
    SourceCitation phosphor_source;

    // beam
    double focus = 0.85;
    IntensityCurve intensity_curve = IntensityCurve::Gauss;
    double scanline_strength = 0.70;
    InterlaceMode interlace_mode = InterlaceMode::Off;

    // glass
    double glass_age = 0.0;
    std::array<double, 3> glass_tint = {1.0, 1.0, 1.0};
    double glass_reflection_strength = 0.05;

    // ageing
    double phosphor_burn_in = 0.0;
    double purity_drift = 0.0;
    double geometry_warp = 0.0;

    // timing
    double h_freq_khz = 15.734;
    double v_freq_hz = 59.94;
};

// Parse helpers (return std::nullopt + error in `error_out` on failure).
std::optional<MaskType>        parse_mask_type        (std::string_view s, std::string& error_out);
std::optional<PhosphorType>    parse_phosphor_type    (std::string_view s, std::string& error_out);
std::optional<ScreenCurvature> parse_screen_curvature (std::string_view s, std::string& error_out);
std::optional<IntensityCurve>  parse_intensity_curve  (std::string_view s, std::string& error_out);
std::optional<InterlaceMode>   parse_interlace_mode   (std::string_view s, std::string& error_out);

const char* mask_type_to_string(MaskType m);
const char* phosphor_type_to_string(PhosphorType p);

} // namespace tubelight
