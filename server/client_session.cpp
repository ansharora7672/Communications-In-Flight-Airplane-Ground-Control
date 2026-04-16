#include "client_session.h"

namespace server {

ClientSession::ClientSession(SocketHandle socketHandle) : socketHandle(socketHandle) {}

SocketHandle ClientSession::socket() const {
    return socketHandle;
}

void ClientSession::setSocket(SocketHandle value) {
    socketHandle = value;
}

void ClientSession::close() {
    shutdownSocket(socketHandle);
    closeSocket(socketHandle);
    socketHandle = INVALID_SOCK;
}

const std::string& ClientSession::aircraftId() const {
    return id;
}

void ClientSession::setAircraftId(const std::string& value) {
    id = value;
}

StateMachine& ClientSession::stateMachine() {
    return machine;
}

const StateMachine& ClientSession::stateMachine() const {
    return machine;
}

const TelemetryPayload& ClientSession::telemetry() const {
    return latestTelemetry;
}

bool ClientSession::hasTelemetry() const {
    return telemetryValid;
}

void ClientSession::updateTelemetry(const TelemetryPayload& payload) {
    latestTelemetry = payload;
    telemetryValid = true;
}

std::uint32_t ClientSession::serverSequence() const {
    return nextServerSequence;
}

std::uint32_t ClientSession::consumeServerSequence() {
    return nextServerSequence++;
}

std::uint32_t ClientSession::lastClientSequence() const {
    return lastClientSequenceNumber;
}

void ClientSession::markPacketReceived(std::uint32_t sequenceNumber) {
    lastClientSequenceNumber = sequenceNumber;
    touch();
}

void ClientSession::touch(std::chrono::steady_clock::time_point now) {
    lastPacketTime = now;
}

bool ClientSession::timedOut(std::chrono::steady_clock::time_point now) const {
    const StateMachine::State state = machine.getState();
    if (state == StateMachine::State::DISCONNECTED || state == StateMachine::State::FAULT) {
        return false;
    }

    const long timeoutSeconds = state == StateMachine::State::TELEMETRY ? 3L : 5L;
    return std::chrono::duration_cast<std::chrono::seconds>(now - lastPacketTime).count() >= timeoutSeconds;
}

} // namespace server
