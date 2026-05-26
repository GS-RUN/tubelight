// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN

#include "profile/validator.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>

namespace tubelight {

namespace {

using nlohmann::json;

// ---- small helpers -------------------------------------------------------

bool has(const json& j, const char* key) {
    return j.contains(key);
}

void require(const json& j, const char* key, const char* context, std::vector<std::string>& errors) {
    if (!j.contains(key)) {
        errors.emplace_back(std::string(context) + ": missing required field '" + key + "'");
    }
}

void require_string(const json& j, const char* key, const char* context, std::vector<std::string>& errors) {
    require(j, key, context, errors);
    if (j.contains(key) && !j.at(key).is_string()) {
        errors.emplace_back(std::string(context) + ": field '" + key + "' must be a string");
    }
}

void require_number(const json& j, const char* key, const char* context, std::vector<std::string>& errors) {
    require(j, key, context, errors);
    if (j.contains(key) && !j.at(key).is_number()) {
        errors.emplace_back(std::string(context) + ": field '" + key + "' must be a number");
    }
}

void require_object(const json& j, const char* key, const char* context, std::vector<std::string>& errors) {
    require(j, key, context, errors);
    if (j.contains(key) && !j.at(key).is_object()) {
        errors.emplace_back(std::string(context) + ": field '" + key + "' must be an object");
    }
}

bool in_range(double v, double lo, double hi) {
    return v >= lo && v <= hi;
}

void check_range(const json& j, const char* key, double lo, double hi,
                 const char* context, std::vector<std::string>& errors) {
    if (j.contains(key) && j.at(key).is_number()) {
        double v = j.at(key).get<double>();
        if (!in_range(v, lo, hi)) {
            std::ostringstream os;
            os << context << ": field '" << key << "' = " << v
               << " out of range [" << lo << ", " << hi << "]";
            errors.emplace_back(os.str());
        }
    }
}

void validate_source_object(const json& j, const char* context,
                            std::vector<std::string>& errors,
                            std::vector<std::string>& warnings) {
    if (!j.is_object()) {
        errors.emplace_back(std::string(context) + ": source must be an object");
        return;
    }
    require_string(j, "url",          context, errors);
    require_string(j, "retrieved_at", context, errors);

    if (j.contains("url") && j.at("url").is_string()) {
        const std::string& url = j.at("url").get_ref<const std::string&>();
        if (url.empty()) {
            errors.emplace_back(std::string(context) + ": source.url cannot be empty (C2)");
        } else if (url.find("://") == std::string::npos) {
            warnings.emplace_back(std::string(context) + ": source.url does not look like a URL (missing scheme)");
        }
    }
}

// ---- CRTProfile validation ----------------------------------------------

void validate_crt_profile(const json& j, ValidationResult& result, CRTProfile& out) {
    auto& errors   = result.errors;
    auto& warnings = result.warnings;

    require_string(j, "id",           "crt_profile",           errors);
    require_string(j, "display_name", "crt_profile",           errors);
    require_string(j, "era",          "crt_profile",           errors);
    require_object(j, "tube",         "crt_profile",           errors);
    require_object(j, "phosphor",     "crt_profile",           errors);
    require_object(j, "beam",         "crt_profile",           errors);
    require_number(j, "h_freq_khz",   "crt_profile",           errors);
    require_number(j, "v_freq_hz",    "crt_profile",           errors);

    if (j.contains("id"))           out.id = j.at("id").get<std::string>();
    if (j.contains("display_name")) out.display_name = j.at("display_name").get<std::string>();
    if (j.contains("era"))          out.era = j.at("era").get<std::string>();

    // tube
    if (j.contains("tube") && j.at("tube").is_object()) {
        const auto& tube = j.at("tube");
        require_string(tube, "mask_type",         "crt_profile.tube", errors);
        require_string(tube, "screen_curvature",  "crt_profile.tube", errors);
        require_number(tube, "diagonal_inches",   "crt_profile.tube", errors);
        require_string(tube, "aspect_native",     "crt_profile.tube", errors);

        if (!tube.contains("dot_pitch_mm")) {
            errors.emplace_back("crt_profile.tube: missing dot_pitch_mm (use null for NEEDS-MEASUREMENT)");
        } else if (tube.at("dot_pitch_mm").is_number()) {
            double v = tube.at("dot_pitch_mm").get<double>();
            if (v <= 0.0) {
                errors.emplace_back("crt_profile.tube.dot_pitch_mm must be > 0 or null");
            } else {
                out.dot_pitch_mm = v;
            }
        } else if (tube.at("dot_pitch_mm").is_null()) {
            warnings.emplace_back("crt_profile.tube.dot_pitch_mm = null (NEEDS-MEASUREMENT)");
        } else {
            errors.emplace_back("crt_profile.tube.dot_pitch_mm must be a number or null");
        }

        if (!tube.contains("source")) {
            errors.emplace_back("crt_profile.tube: missing source (C2)");
        } else {
            validate_source_object(tube.at("source"), "crt_profile.tube.source", errors, warnings);
            if (tube.at("source").is_object()) {
                out.tube_source.url          = tube.at("source").value("url", "");
                out.tube_source.retrieved_at = tube.at("source").value("retrieved_at", "");
                out.tube_source.notes        = tube.at("source").value("notes", "");
            }
        }

        if (tube.contains("mask_type") && tube.at("mask_type").is_string()) {
            std::string err;
            auto mt = parse_mask_type(tube.at("mask_type").get<std::string>(), err);
            if (!mt) errors.emplace_back("crt_profile.tube: " + err);
            else     out.mask_type = *mt;
        }
        if (tube.contains("screen_curvature") && tube.at("screen_curvature").is_string()) {
            std::string err;
            auto sc = parse_screen_curvature(tube.at("screen_curvature").get<std::string>(), err);
            if (!sc) errors.emplace_back("crt_profile.tube: " + err);
            else     out.screen_curvature = *sc;
        }
        if (tube.contains("diagonal_inches") && tube.at("diagonal_inches").is_number()) {
            double v = tube.at("diagonal_inches").get<double>();
            if (v <= 0.0) errors.emplace_back("crt_profile.tube.diagonal_inches must be > 0");
            else out.diagonal_inches = v;
        }
        if (tube.contains("aspect_native") && tube.at("aspect_native").is_string()) {
            out.aspect_native = tube.at("aspect_native").get<std::string>();
        }
    }

    // phosphor
    if (j.contains("phosphor") && j.at("phosphor").is_object()) {
        const auto& ph = j.at("phosphor");
        require_string(ph, "type",     "crt_profile.phosphor", errors);
        require_object(ph, "decay_ms", "crt_profile.phosphor", errors);

        if (ph.contains("type") && ph.at("type").is_string()) {
            std::string err;
            auto p = parse_phosphor_type(ph.at("type").get<std::string>(), err);
            if (!p) errors.emplace_back("crt_profile.phosphor: " + err);
            else    out.phosphor_type = *p;
        }
        if (ph.contains("decay_ms") && ph.at("decay_ms").is_object()) {
            const auto& d = ph.at("decay_ms");
            require_number(d, "r", "crt_profile.phosphor.decay_ms", errors);
            require_number(d, "g", "crt_profile.phosphor.decay_ms", errors);
            require_number(d, "b", "crt_profile.phosphor.decay_ms", errors);
            if (d.contains("r") && d.at("r").is_number()) out.decay_ms_r = d.at("r").get<double>();
            if (d.contains("g") && d.at("g").is_number()) out.decay_ms_g = d.at("g").get<double>();
            if (d.contains("b") && d.at("b").is_number()) out.decay_ms_b = d.at("b").get<double>();
            if (out.decay_ms_r < 0 || out.decay_ms_g < 0 || out.decay_ms_b < 0) {
                errors.emplace_back("crt_profile.phosphor.decay_ms values must be >= 0");
            }
        }
        if (!ph.contains("source")) {
            errors.emplace_back("crt_profile.phosphor: missing source (C2)");
        } else {
            validate_source_object(ph.at("source"), "crt_profile.phosphor.source", errors, warnings);
            if (ph.at("source").is_object()) {
                out.phosphor_source.url          = ph.at("source").value("url", "");
                out.phosphor_source.retrieved_at = ph.at("source").value("retrieved_at", "");
                out.phosphor_source.notes        = ph.at("source").value("notes", "");
            }
        }
    }

    // beam
    if (j.contains("beam") && j.at("beam").is_object()) {
        const auto& b = j.at("beam");
        require_number(b, "focus",             "crt_profile.beam", errors);
        require_string(b, "intensity_curve",   "crt_profile.beam", errors);
        require_number(b, "scanline_strength", "crt_profile.beam", errors);
        require_string(b, "interlace_mode",    "crt_profile.beam", errors);
        check_range   (b, "focus",             0.0, 1.0, "crt_profile.beam", errors);
        check_range   (b, "scanline_strength", 0.0, 1.0, "crt_profile.beam", errors);
        if (b.contains("focus") && b.at("focus").is_number())
            out.focus = b.at("focus").get<double>();
        if (b.contains("scanline_strength") && b.at("scanline_strength").is_number())
            out.scanline_strength = b.at("scanline_strength").get<double>();
        if (b.contains("intensity_curve") && b.at("intensity_curve").is_string()) {
            std::string err;
            auto v = parse_intensity_curve(b.at("intensity_curve").get<std::string>(), err);
            if (!v) errors.emplace_back("crt_profile.beam: " + err);
            else    out.intensity_curve = *v;
        }
        if (b.contains("interlace_mode") && b.at("interlace_mode").is_string()) {
            std::string err;
            auto v = parse_interlace_mode(b.at("interlace_mode").get<std::string>(), err);
            if (!v) errors.emplace_back("crt_profile.beam: " + err);
            else    out.interlace_mode = *v;
        }
    }

    if (j.contains("h_freq_khz") && j.at("h_freq_khz").is_number()) {
        out.h_freq_khz = j.at("h_freq_khz").get<double>();
        if (out.h_freq_khz <= 0) errors.emplace_back("crt_profile.h_freq_khz must be > 0");
    }
    if (j.contains("v_freq_hz") && j.at("v_freq_hz").is_number()) {
        out.v_freq_hz = j.at("v_freq_hz").get<double>();
        if (out.v_freq_hz <= 0) errors.emplace_back("crt_profile.v_freq_hz must be > 0");
    }

    // ---- glass (optional) ------------------------------------------------
    // The "glass" block carries CRT-glass tint and age for the composition
    // pass. Earlier revisions of the validator skipped it, which meant the
    // monochrome / B&W profiles (tv-bw-p4, terminal-p3-amber, ...) never
    // actually painted with their declared tint.
    if (j.contains("glass") && j.at("glass").is_object()) {
        const auto& g = j.at("glass");
        if (g.contains("age") && g.at("age").is_number()) {
            out.glass_age = g.at("age").get<double>();
            if (out.glass_age < 0.0 || out.glass_age > 1.0) {
                warnings.emplace_back("crt_profile.glass.age outside [0, 1]");
            }
        }
        if (g.contains("tint") && g.at("tint").is_array() && g.at("tint").size() == 3) {
            const auto& t = g.at("tint");
            for (size_t i = 0; i < 3; ++i) {
                if (t.at(i).is_number()) out.glass_tint[i] = t.at(i).get<double>();
            }
        }
        if (g.contains("reflection_strength") && g.at("reflection_strength").is_number()) {
            out.glass_reflection_strength = g.at("reflection_strength").get<double>();
        }
    }
}

// ---- SignalProfile validation -------------------------------------------

void validate_signal_profile(const json& j, ValidationResult& result, SignalProfile& out) {
    auto& errors   = result.errors;
    auto& warnings = result.warnings;

    require_string(j, "id",           "signal_profile",        errors);
    require_string(j, "display_name", "signal_profile",        errors);
    require_string(j, "connection",   "signal_profile",        errors);
    require_string(j, "standard",     "signal_profile",        errors);
    require_object(j, "bandwidth",    "signal_profile",        errors);
    require_object(j, "artifacts",    "signal_profile",        errors);

    if (j.contains("id"))           out.id = j.at("id").get<std::string>();
    if (j.contains("display_name")) out.display_name = j.at("display_name").get<std::string>();

    if (j.contains("connection") && j.at("connection").is_string()) {
        std::string err;
        auto c = parse_connection(j.at("connection").get<std::string>(), err);
        if (!c) errors.emplace_back("signal_profile: " + err);
        else    out.connection = *c;
    }
    if (j.contains("standard") && j.at("standard").is_string()) {
        std::string err;
        auto s = parse_standard(j.at("standard").get<std::string>(), err);
        if (!s) errors.emplace_back("signal_profile: " + err);
        else    out.standard = *s;
    }

    if (j.contains("bandwidth") && j.at("bandwidth").is_object()) {
        const auto& bw = j.at("bandwidth");
        require_number(bw, "luma_mhz",     "signal_profile.bandwidth", errors);
        require_number(bw, "chroma_i_mhz", "signal_profile.bandwidth", errors);
        require_number(bw, "chroma_q_mhz", "signal_profile.bandwidth", errors);
        if (bw.contains("luma_mhz") && bw.at("luma_mhz").is_number())
            out.luma_mhz = bw.at("luma_mhz").get<double>();
        if (bw.contains("chroma_i_mhz") && bw.at("chroma_i_mhz").is_number())
            out.chroma_i_mhz = bw.at("chroma_i_mhz").get<double>();
        if (bw.contains("chroma_q_mhz") && bw.at("chroma_q_mhz").is_number())
            out.chroma_q_mhz = bw.at("chroma_q_mhz").get<double>();
        if (out.luma_mhz <= 0) errors.emplace_back("signal_profile.bandwidth.luma_mhz must be > 0");
        if (out.chroma_i_mhz < 0 || out.chroma_q_mhz < 0)
            errors.emplace_back("signal_profile.bandwidth chroma_* must be >= 0");
        if (!bw.contains("source")) {
            errors.emplace_back("signal_profile.bandwidth: missing source (C2)");
        } else {
            validate_source_object(bw.at("source"), "signal_profile.bandwidth.source", errors, warnings);
            if (bw.at("source").is_object()) {
                out.bandwidth_source.url          = bw.at("source").value("url", "");
                out.bandwidth_source.retrieved_at = bw.at("source").value("retrieved_at", "");
                out.bandwidth_source.notes        = bw.at("source").value("notes", "");
            }
        }
    }

    if (j.contains("artifacts") && j.at("artifacts").is_object()) {
        const auto& a = j.at("artifacts");
        check_range(a, "dot_crawl_strength",     0.0, 1.0, "signal_profile.artifacts", errors);
        check_range(a, "rainbow_banding",        0.0, 1.0, "signal_profile.artifacts", errors);
        check_range(a, "ringing_amount",         0.0, 1.0, "signal_profile.artifacts", errors);
        check_range(a, "noise_strength",         0.0, 1.0, "signal_profile.artifacts", errors);
        require_string(a, "noise_type", "signal_profile.artifacts", errors);
        require_number(a, "ghosting_offset_pixels", "signal_profile.artifacts", errors);
        if (a.contains("dot_crawl_strength") && a.at("dot_crawl_strength").is_number())
            out.dot_crawl_strength = a.at("dot_crawl_strength").get<double>();
        if (a.contains("rainbow_banding") && a.at("rainbow_banding").is_number())
            out.rainbow_banding = a.at("rainbow_banding").get<double>();
        if (a.contains("ringing_amount") && a.at("ringing_amount").is_number())
            out.ringing_amount = a.at("ringing_amount").get<double>();
        if (a.contains("ghosting_offset_pixels") && a.at("ghosting_offset_pixels").is_number())
            out.ghosting_offset_pixels = a.at("ghosting_offset_pixels").get<double>();
        if (a.contains("noise_type") && a.at("noise_type").is_string()) {
            std::string err;
            auto n = parse_noise_type(a.at("noise_type").get<std::string>(), err);
            if (!n) errors.emplace_back("signal_profile.artifacts: " + err);
            else    out.noise_type = *n;
        }
        if (a.contains("noise_strength") && a.at("noise_strength").is_number())
            out.noise_strength = a.at("noise_strength").get<double>();
    }

    if (j.contains("effective_tvl")  && j.at("effective_tvl").is_number())
        out.effective_tvl  = j.at("effective_tvl").get<double>();
    if (j.contains("subcarrier_mhz") && j.at("subcarrier_mhz").is_number())
        out.subcarrier_mhz = j.at("subcarrier_mhz").get<double>();
    if (j.contains("h_freq_khz")     && j.at("h_freq_khz").is_number())
        out.h_freq_khz     = j.at("h_freq_khz").get<double>();
}

// ---- kind detection -----------------------------------------------------

ProfileKind detect_kind(const json& j) {
    if (j.contains("tube") && j.contains("phosphor")) return ProfileKind::Crt;
    if (j.contains("connection") && j.contains("bandwidth")) return ProfileKind::Signal;
    return ProfileKind::Unknown;
}

} // namespace

ValidationResult validate_profile_file(const std::string& path) {
    ParsedProfile sink;
    return validate_profile_file(path, sink);
}

ValidationResult validate_profile_file(const std::string& path, ParsedProfile& parsed_out) {
    ValidationResult result;
    parsed_out.kind = ProfileKind::Unknown;

    std::ifstream in(path);
    if (!in.is_open()) {
        result.errors.emplace_back("cannot open file: " + path);
        return result;
    }

    json j;
    try {
        in >> j;
    } catch (const json::parse_error& e) {
        result.errors.emplace_back(std::string("JSON parse error: ") + e.what());
        return result;
    }

    if (!j.is_object()) {
        result.errors.emplace_back("top-level must be an object");
        return result;
    }

    result.kind = detect_kind(j);
    parsed_out.kind = result.kind;
    if (result.kind == ProfileKind::Crt) {
        CRTProfile cp{};
        validate_crt_profile(j, result, cp);
        parsed_out.data = cp;
    } else if (result.kind == ProfileKind::Signal) {
        SignalProfile sp{};
        validate_signal_profile(j, result, sp);
        parsed_out.data = sp;
    } else {
        result.errors.emplace_back(
            "cannot determine profile kind — expected fields 'tube'+'phosphor' (CRT) or 'connection'+'bandwidth' (Signal)");
        return result;
    }

    result.ok = result.errors.empty();
    return result;
}

int print_validation_result(const std::string& path, const ValidationResult& r) {
    const char* kind = (r.kind == ProfileKind::Crt) ? "CRT profile"
                     : (r.kind == ProfileKind::Signal) ? "Signal profile"
                     : "unknown";
    if (r.ok) {
        std::printf("[OK] %s — %s\n", path.c_str(), kind);
        for (const auto& w : r.warnings) {
            std::printf("    warning: %s\n", w.c_str());
        }
        return 0;
    }
    std::fprintf(stderr, "[E_PROFILE_INVALID] %s — %s\n", path.c_str(), kind);
    for (const auto& e : r.errors) {
        std::fprintf(stderr, "    error: %s\n", e.c_str());
    }
    for (const auto& w : r.warnings) {
        std::fprintf(stderr, "    warning: %s\n", w.c_str());
    }
    return 3; // E_PROFILE_INVALID per CONTRACTS.md C4
}

} // namespace tubelight
