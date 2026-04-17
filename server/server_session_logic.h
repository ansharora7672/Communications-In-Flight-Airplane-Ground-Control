#pragma once

#include <string>
#include <vector>

#include "packet.h"
#include "socket_utils.h"
#include "state_machine.h"

namespace server {

struct ActiveSessionSummary {
    SocketHandle socketHandle = INVALID_SOCK;
    std::string aircraftId;
    StateMachine::State state = StateMachine::State::DISCONNECTED;
};

std::string compactPacketLog(const std::string& timestamp, const std::string& direction, const PacketHeader& header);
bool aircraftIdInUse(
    const std::vector<ActiveSessionSummary>& sessions,
    SocketHandle currentSocket,
    const std::string& aircraftId);
bool isValidHandshakeRequest(const PacketHeader& header, const std::string& aircraftId, bool aircraftIdAlreadyInUse);
std::string ignoredOperatorRequestMessage(
    const std::string& aircraftId,
    StateMachine::State state,
    const std::string& action);
std::string faultDashboardEntry(const std::string& timestamp, const std::string& aircraftId, const std::string& cause);

} // namespace server
