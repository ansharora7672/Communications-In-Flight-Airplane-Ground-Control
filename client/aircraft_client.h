#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

#include "client_options.h"
#include "client_utils.h"
#include "flight_sim.h"
#include "logger.h"
#include "packet.h"
#include "socket_utils.h"

namespace client {

class AircraftClient {
public:
    explicit AircraftClient(const ClientOptions& options);
    ~AircraftClient();

    AircraftClient(const AircraftClient&) = delete;
    AircraftClient& operator=(const AircraftClient&) = delete;

    const std::string& aircraftId() const;
    bool aircraftIdExplicit() const;
    bool isRunning() const;

    ClientState state();
    std::string stateMessage();
    void resetToDisconnected();

    void printLine(const std::string& line);
    void printStatus();

    bool connectSession();
    void disconnectSession();
    void closeSocketIfOpen();
    bool requestWeatherMap();
    void telemetryLoop();
    void receiverLoop();

private:
    void setState(ClientState nextState, const std::string& message = std::string());
    bool resolveAndConnect();
    bool sendPacketLocked(const PacketHeader& header, const std::uint8_t* payload);
    void receiveLargeFile(const PacketHeader& header);

    SocketHandle socket = INVALID_SOCK;
    std::atomic<bool> running {false};
    std::atomic<bool> receiverAlive {false};
    std::atomic<bool> fileTransferActive {false};
    std::mutex sendMutex;
    std::mutex consoleMutex;
    std::mutex stateMutex;
    Logger logger {"aircraft"};
    FlightSim flightSim;
    ClientState currentState = ClientState::DISCONNECTED;
    std::string currentStateMessage;
    std::string host;
    std::string id;
    bool explicitId = false;
    std::uint16_t port = kDefaultClientPort;
    std::uint32_t nextSequence = 1;
};

} // namespace client
