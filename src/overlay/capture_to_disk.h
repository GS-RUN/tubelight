// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// Saves the current overlay framebuffer to disk as a PNG, or records a
// sequence of frames to an MP4 via ffmpeg (subprocess pipe). Used by
// Ctrl+Alt+S (screenshot) and Ctrl+Alt+V (video toggle) in the overlay.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tubelight::overlay {

// Reads the default framebuffer (after the pipeline rendered into it) and
// saves it to `out_dir/tubelight-<timestamp>.png`. Returns the absolute
// path on success or empty string on failure.
std::string save_screenshot_png(int width, int height,
                                 const std::string& out_dir,
                                 std::string& error_out);

// Same as save_screenshot_png but the (slow) PNG zlib encode runs on a
// detached background thread, so the caller's main loop stays responsive.
// glReadPixels still runs synchronously on the current GL thread (it has
// to — only that thread owns the context). Returns the destination path
// immediately so the caller can toast / log it; the actual write may not
// be finished yet by the time this returns. Errors are logged to stderr
// by the worker thread.
std::string save_screenshot_png_async(int width, int height,
                                       const std::string& out_dir,
                                       std::string& error_out);

// Returns the user's default capture directory for this platform
// (Pictures\Tubelight on Windows, ~/Pictures/Tubelight on Linux).
std::string default_capture_dir();

class VideoRecorder {
public:
    VideoRecorder() = default;
    ~VideoRecorder() { stop(); }

    VideoRecorder(const VideoRecorder&) = delete;
    VideoRecorder& operator=(const VideoRecorder&) = delete;

    bool is_recording() const { return recording_; }

    // Starts recording at the given resolution into `out_dir/...mp4`.
    // Pipes raw RGB frames into ffmpeg via stdin. Requires ffmpeg in PATH.
    bool start(int width, int height, int fps, const std::string& out_dir,
               std::string& error_out);

    // Pushes one frame from the current default framebuffer (glReadPixels
    // RGB at the recorder's resolution). Used for "overlay view" recording.
    bool push_frame();

    // Pushes one frame from a CPU-side BGRA8 buffer of `src_w`×`src_h`,
    // cropped to (`src_x`, `src_y`, width_, height_). Used to record a
    // monitor sub-rect captured by DXGI directly — bypasses the GL
    // pipeline so we can record areas of the desktop that aren't even
    // visible inside the overlay window. Top-down BGRA8 input is converted
    // to top-down RGB24 for ffmpeg (we pass `-vf vflip` already so the
    // pipe sees what an upright video file should contain).
    bool push_frame_from_bgra(const uint8_t* src, int src_w, int src_h,
                              int src_x, int src_y);

    // Stops the recorder. Idempotent.
    void stop();

    const std::string& output_path() const { return output_path_; }

private:
    bool recording_ = false;
    int  width_ = 0;
    int  height_ = 0;
    std::string output_path_;
    void* ffmpeg_handle_ = nullptr;  // platform-specific (HANDLE on Win, FILE* on Linux)
    // Reused across push_frame() calls so we don't allocate 3*W*H bytes per
    // frame while recording (~370 MB/s churn at 1920x1080@60).
    std::vector<std::uint8_t> frame_buf_;
};

} // namespace tubelight::overlay
