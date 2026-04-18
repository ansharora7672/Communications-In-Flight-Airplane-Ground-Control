#include "client_session.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

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

void ClientSession::updateTelemetry(const TelemetryPayload& payload, std::chrono::steady_clock::time_point now) {
    if (lastTelemetryPacketTime.time_since_epoch().count() != 0) {
        lastTelemetryInterval = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTelemetryPacketTime);
    }
    lastTelemetryPacketTime = now;
    latestTelemetry = payload;
    telemetryValid = true;
    touch(now);
}

bool ClientSession::telemetryRateDegraded(std::chrono::steady_clock::time_point now) const {
    if (machine.getState() != StateMachine::State::TELEMETRY || !telemetryValid ||
        lastTelemetryPacketTime.time_since_epoch().count() == 0 || now <= lastTelemetryPacketTime) {
        return false;
    }

    const auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTelemetryPacketTime);
    const auto threshold = (std::max)(
        std::chrono::milliseconds(1500),
        lastTelemetryInterval.count() > 0 ? lastTelemetryInterval * 2 : std::chrono::milliseconds(0));
    return age >= threshold && age < std::chrono::seconds(3);
}

std::string ClientSession::telemetryAlertMessage(std::chrono::steady_clock::time_point now) const {
    if (!telemetryRateDegraded(now)) {
        return "";
    }

    const auto ageMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTelemetryPacketTime).count();
    std::ostringstream out;
    out << "Telemetry rate degraded: " << std::fixed << std::setprecision(1)
        << (static_cast<double>(ageMilliseconds) / 1000.0) << "s since last packet.";
    return out.str();
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
