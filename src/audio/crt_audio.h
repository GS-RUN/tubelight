// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
// Copyright (c) 2026 Alonso J. Núñez (GS·RUN)
//
// CRT-style audio: continuous flyback transformer whine (~15.7 kHz NTSC,
// ~15.6 kHz PAL) modulated by frame-mean luminance, plus an optional
// low-frequency "thump" when the CRT profile changes (degaussing).
//
// Windows backend: XAudio2 (built into the OS, no external dependency).
// One looping mono buffer that we keep submitted continuously, with a
// per-sample phase accumulator running at the playback sample rate.

#pragma once

#include <cstdint>
#include <string>

namespace tubelight::audio {

class CrtAudio {
public:
    CrtAudio() = default;
    ~CrtAudio() { shutdown(); }

    CrtAudio(const CrtAudio&) = delete;
    CrtAudio& operator=(const CrtAudio&) = delete;

    // Initialise XAudio2 and start the streaming source voice silent.
    // Returns false (and populates `error`) if XAudio2 init fails — the
    // overlay should keep running silently in that case.
    bool init(std::string& error);

    // Tear down voices + the engine.
    void shutdown();

    // Master enable. When false, audio is muted regardless of volume.
    void set_enabled(bool e);
    bool is_enabled() const { return enabled_; }

    // 0.0 .. 1.0 master gain applied on top of luminance modulation.
    void set_volume(float v);
    float volume() const { return master_volume_; }

    // Standard the flyback runs at: NTSC ~15.734 kHz, PAL ~15.625 kHz.
    void set_flyback_frequency_hz(float hz);
    float flyback_frequency_hz() const { return flyback_hz_; }

    // Frame-mean luminance, 0..1. Scales the whine amplitude per frame.
    // CRTs draw more flyback current when the screen is bright, so a
    // 100% white field sounds noticeably louder than a black field.
    void set_frame_luminance(float lum01);

    // Trigger a low-frequency "thump" envelope (degauss). 0..1 strength.
    // Decays naturally over ~0.6s.
    void trigger_degauss(float strength = 1.0f);

private:
    void* engine_ = nullptr;          // IXAudio2*
    void* master_voice_ = nullptr;    // IXAudio2MasteringVoice*
    void* source_voice_ = nullptr;    // IXAudio2SourceVoice*
    void* callback_ = nullptr;        // CrtAudioCallback (defined in .cpp)
    bool  enabled_ = false;
    float master_volume_   = 0.20f;   // intentionally subtle
    float flyback_hz_      = 15734.0f;
    float frame_luminance_ = 0.0f;
};

} // namespace tubelight::audio
