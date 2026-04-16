#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace client {

inline constexpr std::uint16_t kDefaultClientPort = 5000;

struct ClientOptions {
    std::string host = "localhost";
    std::uint16_t port = kDefaultClientPort;
    std::string aircraftId;
    bool aircraftIdExplicit = false;
};

std::optional<ClientOptions> parseClientOptions(int argc, char* argv[]);

} // namespace client
