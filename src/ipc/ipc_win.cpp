// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// IPC implementation for Windows using named pipes.

#if defined(_WIN32)

#include "ipc/ipc.h"

#include <windows.h>

#include <cstdio>
#include <string>

namespace tubelight::ipc {

std::string endpoint_path(std::string_view endpoint) {
    return std::string("\\\\.\\pipe\\tubelight-") + std::string(endpoint);
}

namespace {

class WinServer final : public Server {
public:
    ~WinServer() override { close(); }

    bool listen(std::string_view endpoint) override {
        path_ = endpoint_path(endpoint);
        pipe_ = ::CreateNamedPipeA(
            path_.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1, 4096, 4096, 0, nullptr);
        return pipe_ != INVALID_HANDLE_VALUE;
    }

    void poll(const MessageHandler& handler) override {
        if (pipe_ == INVALID_HANDLE_VALUE) return;
        if (!connected_) {
            BOOL ok = ::ConnectNamedPipe(pipe_, nullptr);
            DWORD err = ::GetLastError();
            if (ok || err == ERROR_PIPE_CONNECTED) connected_ = true;
            else return;
        }
        char buf[1024];
        DWORD bytes = 0;
        DWORD avail = 0;
        if (!::PeekNamedPipe(pipe_, nullptr, 0, nullptr, &avail, nullptr)) return;
        if (avail == 0) return;
        if (!::ReadFile(pipe_, buf, sizeof(buf), &bytes, nullptr)) return;
        rx_buffer_.append(buf, bytes);
        size_t pos;
        while ((pos = rx_buffer_.find('\n')) != std::string::npos) {
            handler(std::string_view(rx_buffer_).substr(0, pos));
            rx_buffer_.erase(0, pos + 1);
        }
    }

    bool send(std::string_view json_line) override {
        if (pipe_ == INVALID_HANDLE_VALUE || !connected_) return false;
        std::string line(json_line); line.push_back('\n');
        DWORD written = 0;
        return ::WriteFile(pipe_, line.data(), static_cast<DWORD>(line.size()), &written, nullptr)
            && written == line.size();
    }

    void close() override {
        if (pipe_ != INVALID_HANDLE_VALUE) {
            ::DisconnectNamedPipe(pipe_);
            ::CloseHandle(pipe_);
            pipe_ = INVALID_HANDLE_VALUE;
        }
        connected_ = false;
    }

private:
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    bool connected_ = false;
    std::string path_;
    std::string rx_buffer_;
};

class WinClient final : public Client {
public:
    ~WinClient() override { close(); }

    bool connect(std::string_view endpoint) override {
        std::string path = endpoint_path(endpoint);
        pipe_ = ::CreateFileA(path.c_str(),
                              GENERIC_READ | GENERIC_WRITE,
                              0, nullptr, OPEN_EXISTING, 0, nullptr);
        return pipe_ != INVALID_HANDLE_VALUE;
    }

    bool send(std::string_view json_line) override {
        if (pipe_ == INVALID_HANDLE_VALUE) return false;
        std::string line(json_line); line.push_back('\n');
        DWORD written = 0;
        return ::WriteFile(pipe_, line.data(), static_cast<DWORD>(line.size()), &written, nullptr)
            && written == line.size();
    }

    void poll(const MessageHandler& handler) override {
        if (pipe_ == INVALID_HANDLE_VALUE) return;
        DWORD avail = 0;
        if (!::PeekNamedPipe(pipe_, nullptr, 0, nullptr, &avail, nullptr)) return;
        if (avail == 0) return;
        char buf[1024];
        DWORD bytes = 0;
        if (!::ReadFile(pipe_, buf, sizeof(buf), &bytes, nullptr)) return;
        rx_buffer_.append(buf, bytes);
        size_t pos;
        while ((pos = rx_buffer_.find('\n')) != std::string::npos) {
            handler(std::string_view(rx_buffer_).substr(0, pos));
            rx_buffer_.erase(0, pos + 1);
        }
    }

    void close() override {
        if (pipe_ != INVALID_HANDLE_VALUE) { ::CloseHandle(pipe_); pipe_ = INVALID_HANDLE_VALUE; }
    }

private:
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    std::string rx_buffer_;
};

} // namespace

Server* create_server() { return new WinServer(); }
Client* create_client() { return new WinClient(); }

} // namespace tubelight::ipc

#endif
