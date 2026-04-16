#include "server_options.h"

#include <exception>
#include <iostream>
#include <string>

namespace server {

std::optional<ServerOptions> parseServerOptions(int argc, char* argv[]) {
    ServerOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--headless") {
            options.headless = true;
            continue;
        }

        try {
            const int parsedPort = std::stoi(arg);
            if (parsedPort < 1 || parsedPort > 65535) {
                std::cerr << "Invalid port. Please choose a value between 1 and 65535.\n";
                return std::nullopt;
            }
            options.listenPort = static_cast<std::uint16_t>(parsedPort);
        } catch (const std::exception&) {
            std::cerr << "Invalid argument. Usage: ./ground_server [port] [--headless]\n";
            return std::nullopt;
        }
    }

    return options;
}

} // namespace server
