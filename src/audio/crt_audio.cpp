// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN

#include "audio/crt_audio.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <xaudio2.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace tubelight::audio {

namespace {

constexpr int   kSampleRate    = 48000;
constexpr int   kChannels      = 1;
constexpr int   kBufferSamples = 2048;   // ~42 ms latency per buffer
constexpr int   kQueuedBuffers = 4;      // keep this many in flight

class CrtAudioCallback : public IXAudio2VoiceCallback {
public:
    std::atomic<int> queued{0};
    void OnBufferEnd(void*) noexcept override { queued.fetch_sub(1); }
    void OnVoiceProcessingPassStart(UINT32) noexcept override {}
    void OnVoiceProcessingPassEnd() noexcept override {}
    void OnStreamEnd() noexcept override {}
    void OnBufferStart(void*) noexcept override {}
    void OnLoopEnd(void*) noexcept override {}
    void OnVoiceError(void*, HRESULT) noexcept override {}
};

// Per-CrtAudio synth state. Held on the heap so the audio thread can keep
// touching it after CrtAudio's destructor runs (we cleanly stop voices
// first, but defensive is cheap).
struct SynthState {
    float sample_rate     = static_cast<float>(kSampleRate);
    float flyback_hz      = 15734.0f;
    std::atomic<float> luminance{0.0f};
    std::atomic<float> master_gain{0.20f};
    std::atomic<bool>  enabled{false};
    std::atomic<float> degauss_env{0.0f};   // 0..1, decays each sample
    float degauss_phase   = 0.0f;
    float carrier_phase   = 0.0f;

    // Sample 1024 mono floats into `out`. Caller owns the buffer.
    void generate(float* out, int n) {
        const float two_pi = 6.28318530718f;
        float lum  = luminance.load(std::memory_order_relaxed);
        float gain = master_gain.load(std::memory_order_relaxed);
        if (!enabled.load(std::memory_order_relaxed)) gain = 0.0f;

        // Map luminance 0..1 → amplitude curve. Even a dark screen still
        // has a tiny baseline flyback whine (the transformer never sleeps).
        float carrier_gain = (0.10f + 0.90f * std::min(std::max(lum, 0.0f), 1.0f)) * gain;
        float dphase_carrier = two_pi * flyback_hz / sample_rate;

        // Degauss: ~30 Hz low rumble with exponential decay envelope.
        float deg_env = degauss_env.load(std::memory_order_relaxed);
        float dphase_thump = two_pi * 35.0f / sample_rate;
        // Decay constant ≈ 0.6 s half-life: env *= 0.5^(1/(0.6*SR))
        float deg_decay_per_sample = std::pow(0.5f, 1.0f / (0.6f * sample_rate));

        for (int i = 0; i < n; ++i) {
            float carrier = std::sin(carrier_phase) * carrier_gain;
            float thump   = std::sin(degauss_phase) * deg_env * 0.8f;
            out[i] = carrier + thump;

            carrier_phase += dphase_carrier;
            if (carrier_phase > two_pi) carrier_phase -= two_pi;
            degauss_phase += dphase_thump;
            if (degauss_phase > two_pi) degauss_phase -= two_pi;
            deg_env *= deg_decay_per_sample;
        }
        degauss_env.store(deg_env, std::memory_order_relaxed);
    }
};

// Each queued submission needs its own buffer (XAudio2 doesn't copy).
struct BufferPool {
    std::vector<std::vector<float>> buffers;
    int next = 0;
    BufferPool() : buffers(kQueuedBuffers) {
        for (auto& b : buffers) b.resize(kBufferSamples, 0.0f);
    }
    std::vector<float>& take() {
        auto& b = buffers[next];
        next = (next + 1) % kQueuedBuffers;
        return b;
    }
};

// Worker that keeps the source voice fed.
struct Worker {
    IXAudio2SourceVoice* voice = nullptr;
    SynthState* synth = nullptr;
    CrtAudioCallback* cb = nullptr;
    std::atomic<bool> running{false};
    std::thread thread;
    BufferPool pool;

    void start() {
        running = true;
        thread = std::thread([this]{
            while (running.load(std::memory_order_relaxed)) {
                if (cb->queued.load() < kQueuedBuffers) {
                    auto& buf = pool.take();
                    synth->generate(buf.data(), kBufferSamples);
                    XAUDIO2_BUFFER xb = {};
                    xb.AudioBytes = static_cast<UINT32>(buf.size() * sizeof(float));
                    xb.pAudioData = reinterpret_cast<const BYTE*>(buf.data());
                    cb->queued.fetch_add(1);
                    voice->SubmitSourceBuffer(&xb);
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            }
        });
    }
    void stop() {
        running = false;
        if (thread.joinable()) thread.join();
    }
};

struct Backend {
    IXAudio2* engine = nullptr;
    IXAudio2MasteringVoice* master = nullptr;
    IXAudio2SourceVoice* source = nullptr;
    CrtAudioCallback callback;
    SynthState synth;
    Worker worker;
    bool com_initialised = false;
};

} // namespace

bool CrtAudio::init(std::string& error) {
    auto* b = new Backend();
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hr == S_OK || hr == S_FALSE) b->com_initialised = true;

    hr = XAudio2Create(&b->engine, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr)) {
        error = "XAudio2Create failed";
        delete b;
        return false;
    }
    hr = b->engine->CreateMasteringVoice(&b->master);
    if (FAILED(hr)) {
        error = "CreateMasteringVoice failed";
        b->engine->Release();
        delete b;
        return false;
    }
    WAVEFORMATEX fmt = {};
    fmt.wFormatTag      = WAVE_FORMAT_IEEE_FLOAT;
    fmt.nChannels       = kChannels;
    fmt.nSamplesPerSec  = kSampleRate;
    fmt.wBitsPerSample  = 32;
    fmt.nBlockAlign     = fmt.nChannels * fmt.wBitsPerSample / 8;
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

    hr = b->engine->CreateSourceVoice(&b->source, &fmt, 0,
                                       XAUDIO2_DEFAULT_FREQ_RATIO, &b->callback);
    if (FAILED(hr)) {
        error = "CreateSourceVoice failed";
        b->master->DestroyVoice();
        b->engine->Release();
        delete b;
        return false;
    }
    b->source->Start(0);

    b->worker.voice = b->source;
    b->worker.synth = &b->synth;
    b->worker.cb    = &b->callback;
    b->worker.start();

    engine_       = b->engine;
    master_voice_ = b->master;
    source_voice_ = b->source;
    callback_     = b;  // we stash the whole backend here for simplicity

    return true;
}

void CrtAudio::shutdown() {
    if (!callback_) return;
    auto* b = static_cast<Backend*>(callback_);
    b->worker.stop();
    if (b->source) {
        b->source->Stop(0);
        b->source->FlushSourceBuffers();
        b->source->DestroyVoice();
    }
    if (b->master) b->master->DestroyVoice();
    if (b->engine) b->engine->Release();
    if (b->com_initialised) CoUninitialize();
    delete b;
    engine_ = nullptr;
    master_voice_ = nullptr;
    source_voice_ = nullptr;
    callback_ = nullptr;
}

void CrtAudio::set_enabled(bool e) {
    enabled_ = e;
    if (callback_) static_cast<Backend*>(callback_)->synth.enabled.store(e);
}

void CrtAudio::set_volume(float v) {
    master_volume_ = std::min(std::max(v, 0.0f), 1.0f);
    if (callback_) static_cast<Backend*>(callback_)->synth.master_gain.store(master_volume_);
}

void CrtAudio::set_flyback_frequency_hz(float hz) {
    flyback_hz_ = hz;
    if (callback_) static_cast<Backend*>(callback_)->synth.flyback_hz = hz;
}

void CrtAudio::set_frame_luminance(float lum01) {
    frame_luminance_ = lum01;
    if (callback_) static_cast<Backend*>(callback_)->synth.luminance.store(lum01);
}

void CrtAudio::trigger_degauss(float strength) {
    if (!callback_) return;
    auto& env = static_cast<Backend*>(callback_)->synth.degauss_env;
    float s = std::min(std::max(strength, 0.0f), 1.0f);
    env.store(s);
}

} // namespace tubelight::audio

#else // !_WIN32 — POSIX stub for now (Linux audio in v1.1)

namespace tubelight::audio {

bool CrtAudio::init(std::string& error) {
    error = "CRT audio on Linux: v1.1 (PipeWire backend)";
    return false;
}
void CrtAudio::shutdown() {}
void CrtAudio::set_enabled(bool e) { enabled_ = e; }
void CrtAudio::set_volume(float v) { master_volume_ = v; }
void CrtAudio::set_flyback_frequency_hz(float hz) { flyback_hz_ = hz; }
void CrtAudio::set_frame_luminance(float lum) { frame_luminance_ = lum; }
void CrtAudio::trigger_degauss(float) {}

} // namespace tubelight::audio

#endif
