#pragma once

#include <cstdint>
#include <optional>

namespace server {

inline constexpr std::uint16_t kDefaultServerPort = 5000;

struct ServerOptions {
    std::uint16_t listenPort = kDefaultServerPort;
    bool headless = false;
};

std::optional<ServerOptions> parseServerOptions(int argc, char* argv[]);

} // namespace server
