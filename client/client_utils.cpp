#include "client_utils.h"

#include <chrono>
#include <iomanip>
#include <regex>
#include <sstream>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace client {

bool validateAircraftId(const std::string& aircraftId) {
    static const std::regex pattern("^AC-[0-9]{3}$");
    return std::regex_match(aircraftId, pattern);
}

namespace {

int processIdValue() {
#ifdef _WIN32
    return _getpid();
#else
    return static_cast<int>(getpid());
#endif
}

std::uint32_t autoAircraftIdBase() {
    static const std::uint32_t base = [] {
        const auto ticks = static_cast<std::uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        std::uint32_t mixed = static_cast<std::uint32_t>(ticks) ^ static_cast<std::uint32_t>(ticks >> 32);
        mixed ^= static_cast<std::uint32_t>(processIdValue() * 2654435761u);
        return mixed % 999u;
    }();
    return base;
}

} // namespace

std::string generateAutoAircraftId(std::uint32_t salt) {
    constexpr std::uint32_t kRetryStride = 590u; // Coprime with 999, so retry salts walk unique candidates.
    const int aircraftNumber = static_cast<int>(((autoAircraftIdBase() + (salt * kRetryStride)) % 999u) + 1u);
    std::ostringstream out;
    out << "AC-" << std::setw(3) << std::setfill('0') << aircraftNumber;
    return out.str();
}

std::string stateToString(ClientState state) {
    switch (state) {
        case ClientState::DISCONNECTED:
            return "DISCONNECTED";
        case ClientState::HANDSHAKE_PENDING:
            return "HANDSHAKE_PENDING";
        case ClientState::CONNECTED:
            return "CONNECTED";
        case ClientState::TELEMETRY:
            return "TELEMETRY";
        case ClientState::LARGE_FILE_TRANSFER:
            return "LARGE_FILE_TRANSFER";
        case ClientState::FAULT:
            return "FAULT";
    }
    return "UNKNOWN";
}

} // namespace client
