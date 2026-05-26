// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN

#include "profile/crt_profile.h"

namespace tubelight {

std::optional<MaskType> parse_mask_type(std::string_view s, std::string& error_out) {
    if (s == "none")            return MaskType::None;
    if (s == "shadow")          return MaskType::Shadow;
    if (s == "aperture_grille") return MaskType::ApertureGrille;
    if (s == "slot")            return MaskType::Slot;
    if (s == "diamond")         return MaskType::Diamond;
    if (s == "cgwg_mix")        return MaskType::CgwgMix;
    if (s == "dot_trio")        return MaskType::DotTrio;
    error_out = std::string("unknown mask_type: ") + std::string(s);
    return std::nullopt;
}

std::optional<PhosphorType> parse_phosphor_type(std::string_view s, std::string& error_out) {
    if (s == "P1")     return PhosphorType::P1;
    if (s == "P3")     return PhosphorType::P3;
    if (s == "P4")     return PhosphorType::P4;
    if (s == "P22")    return PhosphorType::P22;
    if (s == "P31")    return PhosphorType::P31;
    if (s == "custom") return PhosphorType::Custom;
    error_out = std::string("unknown phosphor type: ") + std::string(s);
    return std::nullopt;
}

std::optional<ScreenCurvature> parse_screen_curvature(std::string_view s, std::string& error_out) {
    if (s == "flat")        return ScreenCurvature::Flat;
    if (s == "mild")        return ScreenCurvature::Mild;
    if (s == "aggressive")  return ScreenCurvature::Aggressive;
    error_out = std::string("unknown screen_curvature: ") + std::string(s);
    return std::nullopt;
}

std::optional<IntensityCurve> parse_intensity_curve(std::string_view s, std::string& error_out) {
    if (s == "linear")  return IntensityCurve::Linear;
    if (s == "gauss")   return IntensityCurve::Gauss;
    if (s == "sigmoid") return IntensityCurve::Sigmoid;
    error_out = std::string("unknown intensity_curve: ") + std::string(s);
    return std::nullopt;
}

std::optional<InterlaceMode> parse_interlace_mode(std::string_view s, std::string& error_out) {
    if (s == "off")   return InterlaceMode::Off;
    if (s == "2to1")  return InterlaceMode::TwoToOne;
    if (s == "rf")    return InterlaceMode::Rf;
    error_out = std::string("unknown interlace_mode: ") + std::string(s);
    return std::nullopt;
}

const char* mask_type_to_string(MaskType m) {
    switch (m) {
        case MaskType::None:           return "none";
        case MaskType::Shadow:         return "shadow";
        case MaskType::ApertureGrille: return "aperture_grille";
        case MaskType::Slot:           return "slot";
        case MaskType::Diamond:        return "diamond";
        case MaskType::CgwgMix:        return "cgwg_mix";
        case MaskType::DotTrio:        return "dot_trio";
    }
    return "unknown";
}

const char* phosphor_type_to_string(PhosphorType p) {
    switch (p) {
        case PhosphorType::P1:     return "P1";
        case PhosphorType::P3:     return "P3";
        case PhosphorType::P4:     return "P4";
        case PhosphorType::P22:    return "P22";
        case PhosphorType::P31:    return "P31";
        case PhosphorType::Custom: return "custom";
    }
    return "unknown";
}

} // namespace tubelight
