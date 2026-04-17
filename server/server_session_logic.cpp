#include "server_session_logic.h"

#include "server_utils.h"

namespace server {

namespace {

std::string serverStateLabel(StateMachine::State state) {
    StateMachine machine;
    return machine.stateToString(state);
}

} // namespace

std::string compactPacketLog(const std::string& timestamp, const std::string& direction, const PacketHeader& header) {
    return "[" + timestamp + "] " + direction + ' ' + packetTypeToString(header.packet_type) + " Seq=" +
           std::to_string(header.sequence_number) + ' ' + extractAircraftId(header.aircraft_id);
}

bool aircraftIdInUse(
    const std::vector<ActiveSessionSummary>& sessions,
    SocketHandle currentSocket,
    const std::string& aircraftId) {
    for (const ActiveSessionSummary& session : sessions) {
        if (session.socketHandle == currentSocket) {
            continue;
        }
        if (session.aircraftId == aircraftId && session.state != StateMachine::State::DISCONNECTED) {
            return true;
        }
    }
    return false;
}

bool isValidHandshakeRequest(const PacketHeader& header, const std::string& aircraftId, bool aircraftIdAlreadyInUse) {
    return header.packet_type == PacketType::HANDSHAKE_REQUEST && header.payload_size == 0 &&
           validateAircraftId(aircraftId) && !aircraftIdAlreadyInUse;
}

std::string ignoredOperatorRequestMessage(
    const std::string& aircraftId,
    StateMachine::State state,
    const std::string& action) {
    const std::string effectiveAircraftId = aircraftId.empty() ? "UNKNOWN" : aircraftId;
    return "Ignored operator " + action + " request for " + effectiveAircraftId +
           " because the session is not verified (" + serverStateLabel(state) + ").";
}

std::string faultDashboardEntry(const std::string& timestamp, const std::string& aircraftId, const std::string& cause) {
    const std::string effectiveAircraftId = aircraftId.empty() ? "UNKNOWN" : aircraftId;
    return "[" + timestamp + "] FAULT " + effectiveAircraftId + ' ' + cause;
}

} // namespace server
