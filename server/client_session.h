#pragma once

#include <chrono>
#include <cstdint>
#include <string>

#include "packet.h"
#include "socket_utils.h"
#include "state_machine.h"

namespace server {

class ClientSession {
public:
    explicit ClientSession(SocketHandle socketHandle = INVALID_SOCK);

    SocketHandle socket() const;
    void setSocket(SocketHandle socketHandle);
    void close();

    const std::string& aircraftId() const;
    void setAircraftId(const std::string& value);

    StateMachine& stateMachine();
    const StateMachine& stateMachine() const;

    const TelemetryPayload& telemetry() const;
    bool hasTelemetry() const;
    void updateTelemetry(
        const TelemetryPayload& payload,
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
    bool telemetryRateDegraded(std::chrono::steady_clock::time_point now) const;
    std::string telemetryAlertMessage(std::chrono::steady_clock::time_point now) const;

    std::uint32_t serverSequence() const;
    std::uint32_t consumeServerSequence();
    std::uint32_t lastClientSequence() const;

    void markPacketReceived(std::uint32_t sequenceNumber);
    void touch(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
    bool timedOut(std::chrono::steady_clock::time_point now) const;

private:
    SocketHandle socketHandle = INVALID_SOCK;
    std::string id;
    StateMachine machine;
    TelemetryPayload latestTelemetry {};
    bool telemetryValid = false;
    std::uint32_t nextServerSequence = 1;
    std::uint32_t lastClientSequenceNumber = 0;
    std::chrono::steady_clock::time_point lastPacketTime = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point lastTelemetryPacketTime {};
    std::chrono::milliseconds lastTelemetryInterval {0};
};

} // namespace server
