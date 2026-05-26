// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// IPC implementation for Linux / macOS using Unix domain sockets.

#if !defined(_WIN32)

#include "ipc/ipc.h"

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace tubelight::ipc {

namespace fs = std::filesystem;

std::string endpoint_path(std::string_view endpoint) {
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    fs::path base = runtime ? fs::path(runtime) : fs::path("/tmp");
    return (base / (std::string("tubelight-") + std::string(endpoint) + ".sock")).string();
}

namespace {

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

class UnixServer final : public Server {
public:
    ~UnixServer() override { close(); }

    bool listen(std::string_view endpoint) override {
        const std::string path = endpoint_path(endpoint);
        // Remove stale socket
        unlink(path.c_str());

        listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (listen_fd_ < 0) return false;

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

        if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }
        if (::listen(listen_fd_, 1) < 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }
        set_nonblocking(listen_fd_);
        path_ = path;
        return true;
    }

    void poll(const MessageHandler& handler) override {
        if (listen_fd_ < 0) return;
        if (client_fd_ < 0) {
            int fd = ::accept(listen_fd_, nullptr, nullptr);
            if (fd >= 0) {
                set_nonblocking(fd);
                client_fd_ = fd;
            }
        }
        if (client_fd_ >= 0) {
            char buf[1024];
            ssize_t n = ::read(client_fd_, buf, sizeof(buf));
            if (n > 0) {
                rx_buffer_.append(buf, static_cast<size_t>(n));
                size_t pos;
                while ((pos = rx_buffer_.find('\n')) != std::string::npos) {
                    handler(std::string_view(rx_buffer_).substr(0, pos));
                    rx_buffer_.erase(0, pos + 1);
                }
            } else if (n == 0) {
                ::close(client_fd_);
                client_fd_ = -1;
            }
        }
    }

    bool send(std::string_view json_line) override {
        if (client_fd_ < 0) return false;
        std::string line(json_line);
        line.push_back('\n');
        ssize_t w = ::write(client_fd_, line.data(), line.size());
        return w == static_cast<ssize_t>(line.size());
    }

    void close() override {
        if (client_fd_ >= 0) { ::close(client_fd_); client_fd_ = -1; }
        if (listen_fd_ >= 0) { ::close(listen_fd_); listen_fd_ = -1; }
        if (!path_.empty()) { unlink(path_.c_str()); path_.clear(); }
    }

private:
    int listen_fd_ = -1;
    int client_fd_ = -1;
    std::string path_;
    std::string rx_buffer_;
};

class UnixClient final : public Client {
public:
    ~UnixClient() override { close(); }

    bool connect(std::string_view endpoint) override {
        fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd_ < 0) return false;
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        const std::string path = endpoint_path(endpoint);
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
        if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(fd_); fd_ = -1;
            return false;
        }
        set_nonblocking(fd_);
        return true;
    }

    bool send(std::string_view json_line) override {
        if (fd_ < 0) return false;
        std::string line(json_line);
        line.push_back('\n');
        return ::write(fd_, line.data(), line.size()) == static_cast<ssize_t>(line.size());
    }

    void poll(const MessageHandler& handler) override {
        if (fd_ < 0) return;
        char buf[1024];
        ssize_t n = ::read(fd_, buf, sizeof(buf));
        if (n > 0) {
            rx_buffer_.append(buf, static_cast<size_t>(n));
            size_t pos;
            while ((pos = rx_buffer_.find('\n')) != std::string::npos) {
                handler(std::string_view(rx_buffer_).substr(0, pos));
                rx_buffer_.erase(0, pos + 1);
            }
        } else if (n == 0) {
            ::close(fd_); fd_ = -1;
        }
    }

    void close() override {
        if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    }

private:
    int fd_ = -1;
    std::string rx_buffer_;
};

} // namespace

Server* create_server() { return new UnixServer(); }
Client* create_client() { return new UnixClient(); }

} // namespace tubelight::ipc

#endif
