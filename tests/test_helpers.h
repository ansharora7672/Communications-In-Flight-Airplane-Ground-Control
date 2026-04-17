#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "client_options.h"
#include "server_options.h"
#include "socket_utils.h"

struct SocketRuntimeGuard {
    SocketRuntimeGuard() { initSockets(); }
    ~SocketRuntimeGuard() { cleanupSockets(); }
};

struct ConnectedSocketPair {
    SocketHandle client = INVALID_SOCK;
    SocketHandle server = INVALID_SOCK;

    ConnectedSocketPair() = default;
    ConnectedSocketPair(SocketHandle clientSocket, SocketHandle serverSocket)
        : client(clientSocket), server(serverSocket) {}

    ConnectedSocketPair(const ConnectedSocketPair&) = delete;
    ConnectedSocketPair& operator=(const ConnectedSocketPair&) = delete;

    ConnectedSocketPair(ConnectedSocketPair&& other) noexcept
        : client(other.client), server(other.server) {
        other.client = INVALID_SOCK;
        other.server = INVALID_SOCK;
    }

    ConnectedSocketPair& operator=(ConnectedSocketPair&& other) noexcept {
        if (this != &other) {
            close();
            client = other.client;
            server = other.server;
            other.client = INVALID_SOCK;
            other.server = INVALID_SOCK;
        }
        return *this;
    }

    ~ConnectedSocketPair() { close(); }

    void close() {
        closeSocket(client);
        closeSocket(server);
        client = INVALID_SOCK;
        server = INVALID_SOCK;
    }
};

inline std::filesystem::path makeTempDirectory(const std::string& prefix) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / (prefix + "_" + std::to_string(now));
    std::filesystem::create_directories(directory);
    return directory;
}

inline ConnectedSocketPair makeConnectedSocketPair() {
#ifndef _WIN32
    int sockets[2] = {INVALID_SOCK, INVALID_SOCK};
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == SOCK_ERR) {
        throw std::runtime_error("Unable to create socket pair");
    }
    return ConnectedSocketPair(sockets[0], sockets[1]);
#else
    SocketHandle listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCK) {
        throw std::runtime_error("Unable to create listener socket");
    }

    setReuseAddress(listener);

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCK_ERR) {
        closeSocket(listener);
        throw std::runtime_error("Unable to bind listener socket");
    }

    if (listen(listener, 1) == SOCK_ERR) {
        closeSocket(listener);
        throw std::runtime_error("Unable to listen on listener socket");
    }

    SockLenType addressLength = static_cast<SockLenType>(sizeof(address));
    if (getsockname(listener, reinterpret_cast<sockaddr*>(&address), &addressLength) == SOCK_ERR) {
        closeSocket(listener);
        throw std::runtime_error("Unable to inspect listener socket");
    }

    SocketHandle client = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client == INVALID_SOCK) {
        closeSocket(listener);
        throw std::runtime_error("Unable to create client socket");
    }

    if (connect(client, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCK_ERR) {
        closeSocket(client);
        closeSocket(listener);
        throw std::runtime_error("Unable to connect client socket");
    }

    SocketHandle server =
        accept(listener, reinterpret_cast<sockaddr*>(&address), &addressLength);
    closeSocket(listener);
    if (server == INVALID_SOCK) {
        closeSocket(client);
        throw std::runtime_error("Unable to accept server socket");
    }

    return ConnectedSocketPair(client, server);
#endif
}

inline std::string readFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

inline std::filesystem::path onlyFileIn(const std::filesystem::path& directory) {
    std::filesystem::path result;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (!result.empty()) {
            throw std::runtime_error("Expected exactly one file in directory");
        }
        result = entry.path();
    }
    if (result.empty()) {
        throw std::runtime_error("Expected a file in directory");
    }
    return result;
}

inline std::optional<client::ClientOptions> parseClientArgs(std::vector<std::string> args) {
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (std::string& arg : args) {
        argv.push_back(arg.data());
    }
    return client::parseClientOptions(static_cast<int>(argv.size()), argv.data());
}

inline std::optional<server::ServerOptions> parseServerArgs(std::vector<std::string> args) {
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (std::string& arg : args) {
        argv.push_back(arg.data());
    }
    return server::parseServerOptions(static_cast<int>(argv.size()), argv.data());
}
