#include "client_options.h"

#include <exception>
#include <iostream>

#include "client_utils.h"

namespace client {

std::optional<ClientOptions> parseClientOptions(int argc, char* argv[]) {
    ClientOptions options;
    options.aircraftId = generateAutoAircraftId();

    if (argc > 1) {
        options.host = argv[1];
    }

    if (argc > 2) {
        try {
            const int parsedPort = std::stoi(argv[2]);
            if (parsedPort < 1 || parsedPort > 65535) {
                std::cerr << "Invalid port. Please choose a value between 1 and 65535.\n";
                return std::nullopt;
            }
            options.port = static_cast<std::uint16_t>(parsedPort);
        } catch (const std::exception&) {
            std::cerr << "Invalid client arguments. Usage: ./aircraft_client [host] [port] [aircraft_id]\n";
            return std::nullopt;
        }
    }

    if (argc > 3) {
        options.aircraftId = argv[3];
        options.aircraftIdExplicit = true;
    }

    if (argc > 4) {
        std::cerr << "Invalid client arguments. Usage: ./aircraft_client [host] [port] [aircraft_id]\n";
        return std::nullopt;
    }

    if (!validateAircraftId(options.aircraftId)) {
        std::cerr << "Invalid aircraft id. Use the format AC-001.\n";
        return std::nullopt;
    }

    return options;
}

} // namespace client
