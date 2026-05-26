// SPDX-License-Identifier: MIT
// Copyright (c) 2026 GS-RUN
//
// Cross-platform IPC abstraction (named pipe Win / Unix socket Linux).
// Wire format = JSON line-delimited (`\n` separator), UTF-8.
// Schema documented in specs/CONTRACTS.md §C1.

#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace tubelight::ipc {

// Endpoint name maps to:
//   Windows: \\.\pipe\tubelight-<endpoint>
//   Linux:   $XDG_RUNTIME_DIR/tubelight-<endpoint>.sock
std::string endpoint_path(std::string_view endpoint);

// Callback receives one full JSON line (no trailing \n).
using MessageHandler = std::function<void(std::string_view)>;

class Server {
public:
    Server() = default;
    virtual ~Server() = default;

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Listens on the given endpoint name. Returns false on failure (e.g. pipe
    // already exists, permission denied).
    virtual bool listen(std::string_view endpoint) = 0;

    // Polls / accepts incoming connections and dispatches messages to handler.
    // Non-blocking; intended to be called once per frame from the host loop.
    virtual void poll(const MessageHandler& handler) = 0;

    // Sends a JSON line to the currently connected client (if any).
    virtual bool send(std::string_view json_line) = 0;

    virtual void close() = 0;
};

class Client {
public:
    Client() = default;
    virtual ~Client() = default;

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    virtual bool connect(std::string_view endpoint) = 0;
    virtual bool send(std::string_view json_line) = 0;
    virtual void poll(const MessageHandler& handler) = 0;
    virtual void close() = 0;
};

// Factories — return platform-appropriate concrete implementation.
Server* create_server();
Client* create_client();

} // namespace tubelight::ipc
