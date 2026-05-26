// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN

#include "overlay/capture_to_disk.h"
#include "core/gl_common.h"
#include "io/image_io.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <sstream>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace tubelight::overlay {

namespace fs = std::filesystem;

namespace {

std::string timestamp_now() {
    auto t  = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(t);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm);
    return buf;
}

} // namespace

std::string default_capture_dir() {
#ifdef _WIN32
    char user_profile[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableA("USERPROFILE", user_profile, MAX_PATH);
    fs::path base = (n > 0) ? fs::path(user_profile) : fs::current_path();
    base /= "Pictures";
    base /= "Tubelight";
#else
    const char* home = std::getenv("HOME");
    fs::path base = home ? fs::path(home) : fs::current_path();
    base /= "Pictures";
    base /= "Tubelight";
#endif
    std::error_code ec;
    fs::create_directories(base, ec);
    return base.string();
}

std::string save_screenshot_png(int width, int height,
                                 const std::string& out_dir,
                                 std::string& error_out) {
    if (width <= 0 || height <= 0) {
        error_out = "invalid framebuffer size";
        return {};
    }
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 3);

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    fs::create_directories(out_dir);
    std::string path = (fs::path(out_dir) / ("tubelight-" + timestamp_now() + ".png")).string();
    // glReadPixels delivers bottom-up rows; let stb_image_write do the flip
    // through save_png()'s flip_vertical flag instead of an in-place swap.
    if (!save_png(path, pixels.data(), width, height, 3, error_out, true)) {
        return {};
    }
    return path;
}

// ---------------------------------------------------------------------------
// Video recorder (Windows: pipe to ffmpeg via CreatePipe + CreateProcess).
// ---------------------------------------------------------------------------
#ifdef _WIN32

struct WinPipe {
    HANDLE write_to_ffmpeg = nullptr;
    PROCESS_INFORMATION pi = {};
};

bool VideoRecorder::start(int width, int height, int fps,
                           const std::string& out_dir, std::string& error_out) {
    if (recording_) return true;
    if (width <= 0 || height <= 0) {
        error_out = "invalid resolution";
        return false;
    }

    width_ = width;
    height_ = height;
    fs::create_directories(out_dir);
    output_path_ = (fs::path(out_dir) / ("tubelight-" + timestamp_now() + ".mp4")).string();

    // Build ffmpeg command line.
    std::ostringstream cmd;
    cmd << "ffmpeg -y -f rawvideo -pixel_format rgb24"
        << " -video_size " << width << "x" << height
        << " -framerate "  << fps
        << " -i - "
        << " -vf vflip"                  // flip GL bottom-up rows
        << " -c:v libx264 -pix_fmt yuv420p -preset veryfast -crf 20"
        << " \"" << output_path_ << "\"";
    std::string cmd_str = cmd.str();

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE read_h = nullptr, write_h = nullptr;
    if (!CreatePipe(&read_h, &write_h, &sa, 1 << 20)) {
        error_out = "CreatePipe failed";
        return false;
    }
    SetHandleInformation(write_h, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput  = read_h;
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError  = GetStdHandle(STD_ERROR_HANDLE);

    auto* pipe = new WinPipe();
    std::vector<char> mutable_cmd(cmd_str.begin(), cmd_str.end());
    mutable_cmd.push_back(0);

    BOOL ok = CreateProcessA(nullptr, mutable_cmd.data(), nullptr, nullptr,
                             TRUE, 0, nullptr, nullptr, &si, &pipe->pi);
    CloseHandle(read_h);
    if (!ok) {
        CloseHandle(write_h);
        delete pipe;
        error_out = "CreateProcess for ffmpeg failed (is ffmpeg in PATH?)";
        return false;
    }
    pipe->write_to_ffmpeg = write_h;
    ffmpeg_handle_ = pipe;
    recording_ = true;
    frame_buf_.assign(static_cast<size_t>(width) * height * 3, 0);
    return true;
}

bool VideoRecorder::push_frame() {
    if (!recording_) return false;
    auto* pipe = static_cast<WinPipe*>(ffmpeg_handle_);
    if (!pipe || !pipe->write_to_ffmpeg) return false;

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width_, height_, GL_RGB, GL_UNSIGNED_BYTE, frame_buf_.data());

    DWORD written = 0;
    BOOL ok = WriteFile(pipe->write_to_ffmpeg, frame_buf_.data(),
                        static_cast<DWORD>(frame_buf_.size()), &written, nullptr);
    return ok && written == frame_buf_.size();
}

void VideoRecorder::stop() {
    if (!recording_) return;
    auto* pipe = static_cast<WinPipe*>(ffmpeg_handle_);
    if (pipe) {
        if (pipe->write_to_ffmpeg) {
            CloseHandle(pipe->write_to_ffmpeg);
            pipe->write_to_ffmpeg = nullptr;
        }
        WaitForSingleObject(pipe->pi.hProcess, 5000);
        CloseHandle(pipe->pi.hProcess);
        CloseHandle(pipe->pi.hThread);
        delete pipe;
    }
    ffmpeg_handle_ = nullptr;
    recording_ = false;
}

#else // !_WIN32 — POSIX implementation (popen)

bool VideoRecorder::start(int width, int height, int fps,
                           const std::string& out_dir, std::string& error_out) {
    error_out = "video recording on Linux: v1.1 (popen via libav fork)";
    return false;
}
bool VideoRecorder::push_frame() { return false; }
void VideoRecorder::stop() {}

#endif

} // namespace tubelight::overlay
