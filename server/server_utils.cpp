#include "server_utils.h"

#include <regex>

namespace server {

bool validateAircraftId(const std::string& aircraftId) {
    static const std::regex pattern("^AC-[0-9]{3}$");
    return std::regex_match(aircraftId, pattern);
}

bool isVerifiedSessionState(StateMachine::State state) {
    return state == StateMachine::State::CONNECTED || state == StateMachine::State::TELEMETRY;
}

} // namespace server
