#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "logger.h"
#include "packet.h"
#include "socket_utils.h"

namespace {

constexpr const char* kAircraftId = "AC-001";
constexpr std::uint16_t kDefaultPort = 5000;

enum class ClientState {
    DISCONNECTED,
    HANDSHAKE_PENDING,
    CONNECTED,
    TELEMETRY,
    LARGE_FILE_TRANSFER,
    FAULT
};

struct FlightSim {
    float lat = 43.4643f;
    float lon = -80.5204f;
    float alt = 35000.0f;
    float speed = 480.0f;
    float heading = 270.0f;

    std::mt19937 rng {std::random_device{}()};
    std::uniform_int_distribution<int> drift10 {0, 9};
    std::uniform_int_distribution<int> drift6 {0, 5};

    void tick() {
        lat += static_cast<float>(drift10(rng) - 5) * 0.0001f;
        lon += static_cast<float>(drift10(rng) - 5) * 0.0001f;
        alt += static_cast<float>(drift10(rng) - 5);
        speed += static_cast<float>(drift6(rng) - 3) * 0.5f;
    }
};

struct InputBuffer {
    std::mutex mutex;
    std::deque<std::string> lines;
};

struct ClientSession {
    SocketHandle socket = INVALID_SOCK;
    std::atomic<bool> running {false};
    std::atomic<bool> receiverAlive {false};
    std::atomic<bool> fileTransferActive {false};
    std::mutex sendMutex;
    std::mutex consoleMutex;
    std::mutex stateMutex;
    Logger logger {"aircraft"};
    FlightSim flightSim;
    ClientState state = ClientState::DISCONNECTED;
    std::string stateMessage;
    std::string host;
    std::uint16_t port = kDefaultPort;
    std::uint32_t nextSequence = 1;
};

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

const char* colorPrefix(ClientState state) {
#ifdef _WIN32
    (void)state;
    return "";
#else
    switch (state) {
        case ClientState::HANDSHAKE_PENDING:
            return "\033[33m";
        case ClientState::CONNECTED:
        case ClientState::TELEMETRY:
            return "\033[32m";
        case ClientState::FAULT:
            return "\033[31m";
        default:
            return "\033[0m";
    }
#endif
}

const char* colorSuffix() {
#ifdef _WIN32
    return "";
#else
    return "\033[0m";
#endif
}

void setState(ClientSession& session, ClientState state, const std::string& message = std::string()) {
    std::lock_guard<std::mutex> lock(session.stateMutex);
    session.state = state;
    session.stateMessage = message;
}

ClientState getState(ClientSession& session) {
    std::lock_guard<std::mutex> lock(session.stateMutex);
    return session.state;
}

std::string getStateMessage(ClientSession& session) {
    std::lock_guard<std::mutex> lock(session.stateMutex);
    return session.stateMessage;
}

void printLine(ClientSession& session, const std::string& line) {
    std::lock_guard<std::mutex> lock(session.consoleMutex);
    std::cout << line << std::endl;
}

void printStatus(ClientSession& session) {
    const ClientState state = getState(session);
    std::string statusText = "DISCONNECTED";
    if (state == ClientState::HANDSHAKE_PENDING) {
        statusText = "PENDING";
    } else if (state == ClientState::CONNECTED || state == ClientState::TELEMETRY ||
               state == ClientState::LARGE_FILE_TRANSFER) {
        statusText = "CONNECTED";
    } else if (state == ClientState::FAULT) {
        statusText = "FAULT";
    }

    std::lock_guard<std::mutex> lock(session.consoleMutex);
    std::cout << colorPrefix(state)
              << "Status: " << statusText
              << " | State: " << stateToString(state)
              << " | Aircraft: " << kAircraftId
              << colorSuffix() << std::endl;
    const std::string message = getStateMessage(session);
    if (!message.empty()) {
        std::cout << message << std::endl;
    }
}

bool resolveAndConnect(SocketHandle socketHandle, const std::string& host, std::uint16_t port) {
    addrinfo hints {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    const int status = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &result);
    if (status != 0 || result == nullptr) {
        return false;
    }

    bool connected = false;
    for (addrinfo* current = result; current != nullptr; current = current->ai_next) {
        if (connect(socketHandle, current->ai_addr, static_cast<SockLenType>(current->ai_addrlen)) != SOCK_ERR) {
            connected = true;
            break;
        }
    }

    freeaddrinfo(result);
    return connected;
}

bool sendPacketLocked(ClientSession& session, const PacketHeader& header, const std::uint8_t* payload) {
    std::lock_guard<std::mutex> lock(session.sendMutex);
    if (!sendAll(session.socket, &header, sizeof(header))) {
        return false;
    }
    if (header.payload_size > 0) {
        return sendAll(session.socket, payload, header.payload_size);
    }
    return true;
}

void telemetryLoop(ClientSession& session) {
    while (session.running.load()) {
        if (session.fileTransferActive.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        session.flightSim.tick();
        const TelemetryPayload payload {
            session.flightSim.lat,
            session.flightSim.lon,
            session.flightSim.alt,
            session.flightSim.speed,
            session.flightSim.heading,
        };

        const std::uint8_t* payloadBytes = reinterpret_cast<const std::uint8_t*>(&payload);
        PacketHeader header = makeHeader(
            PacketType::TELEMETRY,
            kAircraftId,
            session.nextSequence++,
            sizeof(TelemetryPayload),
            computeChecksum(payloadBytes, sizeof(TelemetryPayload)));

        if (!sendPacketLocked(session, header, payloadBytes)) {
            setState(session, ClientState::FAULT, "[FAULT] Connection lost. Returning to main menu.");
            session.logger.logFault("TelemetrySendFailure", stateToString(ClientState::TELEMETRY), header.sequence_number);
            session.running.store(false);
            shutdownSocket(session.socket);
            break;
        }

        session.logger.logPacket("TX", header);
        setState(session, ClientState::TELEMETRY);

        std::ostringstream line;
        line << "[TX] SEQ=" << std::setw(4) << std::setfill('0') << header.sequence_number
             << " | LAT=" << std::fixed << std::setprecision(4) << payload.latitude
             << " LON=" << payload.longitude
             << " ALT=" << std::setprecision(0) << payload.altitude << "ft"
             << " SPD=" << std::setprecision(1) << payload.speed << "kts"
             << " HDG=" << std::setprecision(0) << payload.heading << "deg";
        printLine(session, line.str());

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void receiveLargeFile(ClientSession& session, const PacketHeader& header) {
    session.fileTransferActive.store(true);
    setState(session, ClientState::LARGE_FILE_TRANSFER, "[RX] Receiving weather map...");

    LargeFilePayload payload;
    payload.data = static_cast<std::uint8_t*>(std::malloc(header.payload_size));
    payload.size = header.payload_size;
    if (!payload.data) {
        setState(session, ClientState::FAULT, "[FAULT] Unable to allocate buffer for weather map.");
        session.running.store(false);
        return;
    }

    std::uint32_t received = 0;
    while (received < header.payload_size && session.running.load()) {
        const std::uint32_t chunk = std::min<std::uint32_t>(4096, header.payload_size - received);
        const int bytes = recv(session.socket, reinterpret_cast<char*>(payload.data + received), static_cast<int>(chunk), 0);
        if (bytes <= 0) {
            setState(session, ClientState::FAULT, "[FAULT] Connection lost. Returning to main menu.");
            session.logger.logFault("LargeFileReceiveFailure", stateToString(ClientState::LARGE_FILE_TRANSFER), header.sequence_number);
            session.running.store(false);
            session.fileTransferActive.store(false);
            shutdownSocket(session.socket);
            return;
        }
        received += static_cast<std::uint32_t>(bytes);

        std::ostringstream progress;
        progress << "[RX] Receiving weather map... " << received << '/' << header.payload_size << " bytes";
        printLine(session, progress.str());
    }

    if (computeChecksum(payload.data, payload.size) != header.checksum) {
        setState(session, ClientState::FAULT, "[FAULT] Weather map checksum mismatch.");
        session.logger.logFault("ChecksumMismatch", stateToString(ClientState::LARGE_FILE_TRANSFER), header.sequence_number);
        session.running.store(false);
        session.fileTransferActive.store(false);
        shutdownSocket(session.socket);
        return;
    }

    std::ofstream out("received_weather_map.bmp", std::ios::binary);
    out.write(reinterpret_cast<const char*>(payload.data), payload.size);

    session.logger.logPacket("RX", header);
    setState(session, ClientState::CONNECTED, "[CONNECTED] Weather map saved: received_weather_map.bmp");

    std::ostringstream saved;
    saved << "[RX] Weather map saved: received_weather_map.bmp ("
          << std::fixed << std::setprecision(1)
          << (static_cast<double>(payload.size) / (1024.0 * 1024.0))
          << " MB)";
    printLine(session, saved.str());
    session.fileTransferActive.store(false);
}

void receiverLoop(ClientSession& session) {
    session.receiverAlive.store(true);
    while (session.running.load()) {
        if (!waitForReadable(session.socket, 250)) {
            continue;
        }

        PacketHeader header {};
        if (!recvAll(session.socket, &header, sizeof(header))) {
            setState(session, ClientState::FAULT, "[FAULT] Connection lost. Returning to main menu.");
            session.logger.logFault("ReceiveFailure", stateToString(getState(session)), 0);
            session.running.store(false);
            break;
        }

        switch (header.packet_type) {
            case PacketType::LARGE_FILE:
                receiveLargeFile(session, header);
                break;
            case PacketType::DISCONNECT:
                session.logger.logPacket("RX", header);
                setState(session, ClientState::DISCONNECTED, "[INFO] Server disconnected the session.");
                session.running.store(false);
                break;
            default: {
                std::vector<std::uint8_t> payload(header.payload_size);
                if (header.payload_size > 0 && !recvAll(session.socket, payload.data(), header.payload_size)) {
                    setState(session, ClientState::FAULT, "[FAULT] Connection lost. Returning to main menu.");
                    session.logger.logFault("UnexpectedPayloadReceiveFailure", stateToString(getState(session)), 0);
                    session.running.store(false);
                    break;
                }
                session.logger.logPacket("RX", header);
                break;
            }
        }
    }
    session.receiverAlive.store(false);
}

bool connectSession(ClientSession& session) {
    session.socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (session.socket == INVALID_SOCK) {
        setState(session, ClientState::FAULT, "[FAULT] Unable to create client socket.");
        return false;
    }

    setState(session, ClientState::HANDSHAKE_PENDING, "[PENDING] Connecting to ground control...");
    if (!resolveAndConnect(session.socket, session.host, session.port)) {
        setState(session, ClientState::FAULT, "[FAULT] Unable to connect to ground control.");
        closeSocket(session.socket);
        session.socket = INVALID_SOCK;
        return false;
    }

    PacketHeader handshake = makeHeader(PacketType::HANDSHAKE_REQUEST, kAircraftId, session.nextSequence++, 0, 0);
    if (!sendPacketLocked(session, handshake, nullptr)) {
        setState(session, ClientState::FAULT, "[FAULT] Failed to send handshake request.");
        closeSocket(session.socket);
        session.socket = INVALID_SOCK;
        return false;
    }
    session.logger.logPacket("TX", handshake);

    PacketHeader response {};
    if (!recvAll(session.socket, &response, sizeof(response))) {
        setState(session, ClientState::FAULT, "[FAULT] No handshake response received.");
        closeSocket(session.socket);
        session.socket = INVALID_SOCK;
        return false;
    }
    session.logger.logPacket("RX", response);

    if (response.packet_type != PacketType::HANDSHAKE_ACK) {
        setState(session, ClientState::DISCONNECTED, "[FAULT] Handshake rejected by server.");
        closeSocket(session.socket);
        session.socket = INVALID_SOCK;
        return false;
    }

    setState(session, ClientState::CONNECTED, "[CONNECTED] Handshake verified. Channel secure.");
    session.running.store(true);
    return true;
}

void disconnectSession(ClientSession& session) {
    if (session.socket == INVALID_SOCK) {
        return;
    }

    PacketHeader disconnectHeader = makeHeader(PacketType::DISCONNECT, kAircraftId, session.nextSequence++, 0, 0);
    sendPacketLocked(session, disconnectHeader, nullptr);
    session.logger.logPacket("TX", disconnectHeader);

    session.running.store(false);
    shutdownSocket(session.socket);
    closeSocket(session.socket);
    session.socket = INVALID_SOCK;
    session.fileTransferActive.store(false);
}

void inputThreadMain(InputBuffer& inputBuffer) {
    for (std::string line; std::getline(std::cin, line);) {
        std::lock_guard<std::mutex> lock(inputBuffer.mutex);
        inputBuffer.lines.push_back(line);
    }
}

bool tryPopInput(InputBuffer& inputBuffer, std::string& line) {
    std::lock_guard<std::mutex> lock(inputBuffer.mutex);
    if (inputBuffer.lines.empty()) {
        return false;
    }
    line = inputBuffer.lines.front();
    inputBuffer.lines.pop_front();
    return true;
}

std::string normalizeChoice(std::string value) {
    for (char& ch : value) {
        if (ch >= 'a' && ch <= 'z') {
            ch = static_cast<char>(ch - ('a' - 'A'));
        }
    }
    return value;
}

} // namespace

int main(int argc, char* argv[]) {
    initSockets();

    ClientSession session;
    session.host = argc > 1 ? argv[1] : "localhost";
    session.port = argc > 2 ? static_cast<std::uint16_t>(std::stoi(argv[2])) : kDefaultPort;

    {
        std::lock_guard<std::mutex> lock(session.consoleMutex);
        std::cout << "====================================\n";
        std::cout << "  AIRCRAFT CLIENT - " << kAircraftId << '\n';
        std::cout << "====================================\n";
    }

    InputBuffer inputBuffer;
    std::thread(inputThreadMain, std::ref(inputBuffer)).detach();

    bool printedDisconnectedMenu = false;
    bool printedConnectedMenu = false;

    while (true) {
        const bool connected = session.running.load();

        if (!connected) {
            if (session.socket != INVALID_SOCK) {
                closeSocket(session.socket);
                session.socket = INVALID_SOCK;
            }

            printedConnectedMenu = false;
            if (!printedDisconnectedMenu) {
                printStatus(session);
                printLine(session, "[1] Connect to Ground Control  [Q] Quit");
                printedDisconnectedMenu = true;
            }

            std::string choice;
            if (!tryPopInput(inputBuffer, choice)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            choice = normalizeChoice(choice);
            if (choice == "Q") {
                break;
            }
            if (choice == "1") {
                printedDisconnectedMenu = false;
                if (!connectSession(session)) {
                    continue;
                }

                std::thread telemetry(telemetryLoop, std::ref(session));
                std::thread receiver(receiverLoop, std::ref(session));

                printedConnectedMenu = false;
                while (session.running.load()) {
                    if (!printedConnectedMenu) {
                        printStatus(session);
                        printLine(session, "[W] Request Weather Map  [D] Disconnect");
                        printedConnectedMenu = true;
                    }

                    std::string command;
                    if (!tryPopInput(inputBuffer, command)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        continue;
                    }

                    command = normalizeChoice(command);
                    if (command == "W") {
                        PacketHeader request = makeHeader(PacketType::LARGE_FILE, kAircraftId, session.nextSequence++, 0, 0);
                        if (!sendPacketLocked(session, request, nullptr)) {
                            setState(session, ClientState::FAULT, "[FAULT] Unable to request weather map.");
                            session.running.store(false);
                            break;
                        }
                        session.logger.logPacket("TX", request);
                        printLine(session, "[TX] Weather map request sent.");
                        printedConnectedMenu = false;
                    } else if (command == "D") {
                        disconnectSession(session);
                    }
                }

                disconnectSession(session);
                if (telemetry.joinable()) {
                    telemetry.join();
                }
                if (receiver.joinable()) {
                    receiver.join();
                }

                const ClientState state = getState(session);
                if (state == ClientState::FAULT) {
                    printStatus(session);
                }

                setState(session, ClientState::DISCONNECTED);
                printedDisconnectedMenu = false;
            }
        }
    }

    cleanupSockets();
    return 0;
}
