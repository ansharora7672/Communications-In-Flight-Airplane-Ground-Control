#pragma once

#include <cstdint>
#include <string>

namespace client {

enum class ClientState {
    DISCONNECTED,
    HANDSHAKE_PENDING,
    CONNECTED,
    TELEMETRY,
    LARGE_FILE_TRANSFER,
    FAULT
};

bool validateAircraftId(const std::string& aircraftId);
std::string generateAutoAircraftId(std::uint32_t salt = 0);
std::string stateToString(ClientState state);

} // namespace client
