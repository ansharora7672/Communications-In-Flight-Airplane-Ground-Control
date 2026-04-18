#include "aircraft_client.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

namespace client {
namespace {

std::string fileTimestampUtc() {
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    const std::tm* utcNow = std::gmtime(&nowTime);

    std::ostringstream out;
    out << std::put_time(utcNow, "%Y%m%d_%H%M%S")
        << '_'
        << std::setw(3) << std::setfill('0') << milliseconds;
    return out.str();
}

std::string formatSequence(std::uint32_t sequence) {
    std::ostringstream out;
    out << "seq" << std::setw(4) << std::setfill('0') << sequence;
    return out.str();
}

std::string receivedWeatherMapPathFor(const std::string& aircraftId, std::uint32_t sequenceNumber) {
    return "runtime/bitmaps/received/" + fileTimestampUtc() + "_" + aircraftId + "_" +
           formatSequence(sequenceNumber) + "_weather_map.bmp";
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

} // namespace

AircraftClient::AircraftClient(const ClientOptions& options)
    : host(options.host),
      id(options.aircraftId),
      explicitId(options.aircraftIdExplicit),
      port(options.port) {}

AircraftClient::~AircraftClient() {
    running.store(false);
    closeSocketIfOpen();
}

const std::string& AircraftClient::aircraftId() const {
    return id;
}

bool AircraftClient::aircraftIdExplicit() const {
    return explicitId;
}

bool AircraftClient::isRunning() const {
    return running.load();
}

ClientState AircraftClient::state() {
    std::lock_guard<std::mutex> lock(stateMutex);
    return currentState;
}

std::string AircraftClient::stateMessage() {
    std::lock_guard<std::mutex> lock(stateMutex);
    return currentStateMessage;
}

void AircraftClient::resetToDisconnected() {
    setState(ClientState::DISCONNECTED);
}

void AircraftClient::setState(ClientState nextState, const std::string& message) {
    std::lock_guard<std::mutex> lock(stateMutex);
    currentState = nextState;
    currentStateMessage = message;
}

void AircraftClient::printLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(consoleMutex);
    std::cout << line << std::endl;
}

void AircraftClient::printStatus() {
    const ClientState snapshot = state();
    std::string statusText = "DISCONNECTED";
    if (snapshot == ClientState::HANDSHAKE_PENDING) {
        statusText = "PENDING";
    } else if (snapshot == ClientState::CONNECTED || snapshot == ClientState::TELEMETRY ||
               snapshot == ClientState::LARGE_FILE_TRANSFER) {
        statusText = "CONNECTED";
    } else if (snapshot == ClientState::FAULT) {
        statusText = "FAULT";
    }

    std::lock_guard<std::mutex> lock(consoleMutex);
    std::cout << colorPrefix(snapshot)
              << "Status: " << statusText
              << " | State: " << stateToString(snapshot)
              << " | Aircraft: " << id
              << colorSuffix() << std::endl;
    const std::string message = stateMessage();
    if (!message.empty()) {
        std::cout << message << std::endl;
    }
}

bool AircraftClient::resolveAndConnect() {
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
        if (connect(socket, current->ai_addr, static_cast<SockLenType>(current->ai_addrlen)) != SOCK_ERR) {
            connected = true;
            break;
        }
    }

    freeaddrinfo(result);
    return connected;
}

bool AircraftClient::sendPacketLocked(const PacketHeader& header, const std::uint8_t* payload) {
    std::lock_guard<std::mutex> lock(sendMutex);
    if (!sendAll(socket, &header, sizeof(header))) {
        return false;
    }
    if (header.payload_size > 0) {
        return sendAll(socket, payload, header.payload_size);
    }
    return true;
}

bool AircraftClient::connectSession() {
    constexpr std::uint32_t kMaxAutoIdAttempts = 8;
    const std::uint32_t maxAttempts = explicitId ? 1u : kMaxAutoIdAttempts;

    for (std::uint32_t attempt = 0; attempt < maxAttempts; ++attempt) {
        nextSequence = 1;
        socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket == INVALID_SOCK) {
            setState(ClientState::FAULT, "[FAULT] Unable to create client socket.");
            return false;
        }

        setState(ClientState::HANDSHAKE_PENDING, "[PENDING] Connecting to ground control...");
        if (!resolveAndConnect()) {
            setState(ClientState::FAULT, "[FAULT] Unable to connect to ground control.");
            closeSocketIfOpen();
            return false;
        }

        PacketHeader handshake = makeHeader(PacketType::HANDSHAKE_REQUEST, id, nextSequence++, 0, 0);
        if (!sendPacketLocked(handshake, nullptr)) {
            setState(ClientState::FAULT, "[FAULT] Failed to send handshake request.");
            closeSocketIfOpen();
            return false;
        }
        logger.logPacket("TX", handshake);

        PacketHeader response {};
        if (!recvAll(socket, &response, sizeof(response))) {
            setState(ClientState::FAULT, "[FAULT] No handshake response received.");
            closeSocketIfOpen();
            return false;
        }
        logger.logPacket("RX", response);

        if (response.packet_type == PacketType::HANDSHAKE_ACK) {
            setState(ClientState::CONNECTED, "[CONNECTED] Handshake verified. Channel secure.");
            running.store(true);
            return true;
        }

        closeSocketIfOpen();

        if (response.packet_type == PacketType::HANDSHAKE_FAIL && !explicitId && attempt + 1 < maxAttempts) {
            const std::string previousAircraftId = id;
            id = generateAutoAircraftId(attempt + 1);
            printLine(
                "[INFO] Auto-assigned aircraft ID " + previousAircraftId +
                    " was already in use. Retrying as " + id + '.');
            continue;
        }

        if (response.packet_type == PacketType::HANDSHAKE_FAIL) {
            setState(
                ClientState::DISCONNECTED,
                "[FAULT] Handshake rejected by server. Aircraft ID may already be in use.");
        } else {
            setState(ClientState::DISCONNECTED, "[FAULT] Unexpected handshake response from server.");
        }
        return false;
    }

    setState(
        ClientState::DISCONNECTED,
        "[FAULT] Unable to claim a unique aircraft ID automatically. Try passing one explicitly.");
    return false;
}

void AircraftClient::disconnectSession() {
    if (socket == INVALID_SOCK) {
        return;
    }

    const bool wasRunning = running.exchange(false);
    if (wasRunning) {
        PacketHeader disconnectHeader = makeHeader(PacketType::DISCONNECT, id, nextSequence++, 0, 0);
        if (sendPacketLocked(disconnectHeader, nullptr)) {
            logger.logPacket("TX", disconnectHeader);
        }

        setState(ClientState::DISCONNECTED, "[INFO] Disconnected from ground control.");
        fileTransferActive.store(false);
        shutdownSocketSend(socket);
        return;
    }

    closeSocket(socket);
    socket = INVALID_SOCK;
    fileTransferActive.store(false);
}

void AircraftClient::closeSocketIfOpen() {
    if (socket != INVALID_SOCK) {
        closeSocket(socket);
        socket = INVALID_SOCK;
    }
}

bool AircraftClient::requestWeatherMap() {
    PacketHeader request = makeHeader(PacketType::LARGE_FILE, id, nextSequence++, 0, 0);
    if (!sendPacketLocked(request, nullptr)) {
        setState(ClientState::FAULT, "[FAULT] Unable to request weather map.");
        running.store(false);
        return false;
    }
    logger.logPacket("TX", request);
    printLine("[TX] Weather map request sent.");
    return true;
}

void AircraftClient::telemetryLoop() {
    while (running.load()) {
        if (fileTransferActive.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        flightSim.tick();
        const TelemetryPayload payload = flightSim.telemetryPayload();

        const std::uint8_t* payloadBytes = reinterpret_cast<const std::uint8_t*>(&payload);
        PacketHeader header = makeHeader(
            PacketType::TELEMETRY,
            id,
            nextSequence++,
            sizeof(TelemetryPayload),
            computeChecksum(payloadBytes, sizeof(TelemetryPayload)));

        if (!sendPacketLocked(header, payloadBytes)) {
            setState(ClientState::FAULT, "[FAULT] Connection lost. Returning to main menu.");
            logger.logFault("TelemetrySendFailure", stateToString(ClientState::TELEMETRY), header.sequence_number);
            running.store(false);
            shutdownSocket(socket);
            break;
        }

        logger.logPacket("TX", header);
        setState(ClientState::TELEMETRY);

        std::ostringstream line;
        line << "[TX] SEQ=" << std::setw(4) << std::setfill('0') << header.sequence_number
             << " | LAT=" << std::fixed << std::setprecision(4) << payload.latitude
             << " LON=" << payload.longitude
             << " ALT=" << std::setprecision(0) << payload.altitude << "ft"
             << " SPD=" << std::setprecision(1) << payload.speed << "kts"
             << " HDG=" << std::setprecision(0) << payload.heading << "deg";
        printLine(line.str());

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void AircraftClient::receiveLargeFile(const PacketHeader& header) {
    fileTransferActive.store(true);
    setState(ClientState::LARGE_FILE_TRANSFER, "[RX] Receiving weather map...");

    LargeFilePayload payload;
    payload.data = static_cast<std::uint8_t*>(std::malloc(header.payload_size));
    payload.size = header.payload_size;
    if (!payload.data) {
        setState(ClientState::FAULT, "[FAULT] Unable to allocate buffer for weather map.");
        running.store(false);
        return;
    }

    std::uint32_t received = 0;
    while (received < header.payload_size && running.load()) {
        const SocketTransferSize chunkSize =
            static_cast<SocketTransferSize>((std::min<std::uint32_t>)(4096u, header.payload_size - received));
        const auto bytes = recv(
            socket,
            reinterpret_cast<char*>(payload.data + received),
            chunkSize,
            0);
        if (bytes <= 0) {
            if (!running.load()) {
                fileTransferActive.store(false);
                return;
            }
            setState(ClientState::FAULT, "[FAULT] Connection lost. Returning to main menu.");
            logger.logFault("LargeFileReceiveFailure", stateToString(ClientState::LARGE_FILE_TRANSFER), header.sequence_number);
            running.store(false);
            fileTransferActive.store(false);
            shutdownSocket(socket);
            return;
        }
        received += static_cast<std::uint32_t>(bytes);

        std::ostringstream progress;
        progress << "[RX] Receiving weather map... " << received << '/' << header.payload_size << " bytes";
        printLine(progress.str());
    }

    if (computeChecksum(payload.data, payload.size) != header.checksum) {
        setState(ClientState::FAULT, "[FAULT] Weather map checksum mismatch.");
        logger.logFault("ChecksumMismatch", stateToString(ClientState::LARGE_FILE_TRANSFER), header.sequence_number);
        running.store(false);
        fileTransferActive.store(false);
        shutdownSocket(socket);
        return;
    }

    const std::string outputPath = receivedWeatherMapPathFor(id, header.sequence_number);
    std::filesystem::create_directories(std::filesystem::path(outputPath).parent_path());
    std::ofstream out(outputPath, std::ios::binary);
    out.write(reinterpret_cast<const char*>(payload.data), payload.size);

    logger.logPacket("RX", header);
    logger.logInfo("Weather map saved: " + outputPath);
    setState(ClientState::CONNECTED, "[CONNECTED] Weather map saved: " + outputPath);

    std::ostringstream saved;
    saved << "[RX] Weather map saved: " << outputPath << " ("
          << std::fixed << std::setprecision(1)
          << (static_cast<double>(payload.size) / (1024.0 * 1024.0))
          << " MB)";
    printLine(saved.str());
    fileTransferActive.store(false);
}

void AircraftClient::receiverLoop() {
    receiverAlive.store(true);
    while (running.load()) {
        if (!waitForReadable(socket, 250)) {
            continue;
        }

        PacketHeader header {};
        if (!recvAll(socket, &header, sizeof(header))) {
            if (!running.load()) {
                break;
            }
            setState(ClientState::FAULT, "[FAULT] Connection lost. Returning to main menu.");
            logger.logFault("ReceiveFailure", stateToString(state()), 0);
            running.store(false);
            break;
        }

        switch (header.packet_type) {
            case PacketType::LARGE_FILE:
                receiveLargeFile(header);
                break;
            case PacketType::DISCONNECT:
                logger.logPacket("RX", header);
                setState(ClientState::DISCONNECTED, "[INFO] Server disconnected the session.");
                running.store(false);
                break;
            default: {
                std::vector<std::uint8_t> payload(header.payload_size);
                if (header.payload_size > 0 && !recvAll(socket, payload.data(), header.payload_size)) {
                    setState(ClientState::FAULT, "[FAULT] Connection lost. Returning to main menu.");
                    logger.logFault("UnexpectedPayloadReceiveFailure", stateToString(state()), 0);
                    running.store(false);
                    break;
                }
                logger.logPacket("RX", header);
                break;
            }
        }
    }
    receiverAlive.store(false);
}

} // namespace client
