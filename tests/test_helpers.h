#pragma once

#include <optional>
#include <string>
#include <vector>

#include "client_options.h"
#include "server_options.h"

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
