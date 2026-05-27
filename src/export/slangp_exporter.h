// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)
//
// Exports a Tubelight CRT + Signal profile pair as a RetroArch
// .slangp preset file. The exported preset references the same GLSL
// shaders Tubelight uses, so RetroArch users can preview the same look
// without running the standalone overlay.
//
// Format reference:
//   https://github.com/libretro/slang-shaders/blob/master/docs/preset_spec.md

#pragma once

#include "profile/crt_profile.h"
#include "profile/signal_profile.h"

#include <string>

namespace tubelight::exporter {

bool export_slangp(const CRTProfile& crt,
                   const SignalProfile& signal,
                   const std::string& output_path,
                   std::string& error_out);

} // namespace tubelight::exporter
