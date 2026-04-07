#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_impl_opengl3_loader.h"

#include "imgui_dashboard.h"
#include "logger.h"
#include "packet.h"
#include "socket_utils.h"
#include "state_machine.h"

namespace {

constexpr std::uint16_t kDefaultPort = 5000;
constexpr std::size_t kChunkSize = 4096;
const std::string kWeatherMapPath = "runtime/bitmaps/generated/weather_map.bmp";

struct SharedServerState {
    std::mutex mutex;
    DashboardState dashboard;
};

struct ServerOptions {
    std::uint16_t listenPort = kDefaultPort;
    bool headless = false;
};

std::string timeStampHmsUtc() {
    const std::time_t now = std::time(nullptr);
    const std::tm* utcNow = std::gmtime(&now);
    std::ostringstream out;
    out << std::put_time(utcNow, "%H:%M:%S");
    return out.str();
}

void appendDashboardLog(SharedServerState& sharedState, const std::string& entry) {
    std::lock_guard<std::mutex> lock(sharedState.mutex);
    sharedState.dashboard.recentLogEntries.push_front(entry);
    if (sharedState.dashboard.recentLogEntries.size() > 20) {
        sharedState.dashboard.recentLogEntries.pop_back();
    }
}

void setDashboardConnection(
    SharedServerState& sharedState,
    StateMachine::State state,
    bool connected,
    const std::string& aircraftId,
    const std::string& alertMessage = std::string()) {
    std::lock_guard<std::mutex> lock(sharedState.mutex);
    sharedState.dashboard.state = state;
    sharedState.dashboard.connected = connected;
    sharedState.dashboard.aircraftId = aircraftId.empty() ? "N/A" : aircraftId;
    sharedState.dashboard.alertMessage = alertMessage;
    if (state == StateMachine::State::DISCONNECTED) {
        sharedState.dashboard.telemetryValid = false;
    }
}

void updateTelemetry(SharedServerState& sharedState, const TelemetryPayload& payload) {
    std::lock_guard<std::mutex> lock(sharedState.mutex);
    sharedState.dashboard.telemetry = payload;
    sharedState.dashboard.telemetryValid = true;
    sharedState.dashboard.state = StateMachine::State::TELEMETRY;
    sharedState.dashboard.connected = true;
}

void clearDashboardFlags(SharedServerState& sharedState, bool& sendWeatherMap, bool& disconnectRequested) {
    std::lock_guard<std::mutex> lock(sharedState.mutex);
    sendWeatherMap = sharedState.dashboard.requestSendWeather;
    disconnectRequested = sharedState.dashboard.requestDisconnect;
    sharedState.dashboard.requestSendWeather = false;
    sharedState.dashboard.requestDisconnect = false;
}

bool dashboardShowsFault(SharedServerState& sharedState) {
    std::lock_guard<std::mutex> lock(sharedState.mutex);
    return sharedState.dashboard.state == StateMachine::State::FAULT;
}

std::string compactPacketLog(const std::string& direction, const PacketHeader& header) {
    std::ostringstream line;
    line << "[" << timeStampHmsUtc() << "] "
         << direction << ' '
         << packetTypeToString(header.packet_type)
         << " Seq=" << header.sequence_number
         << ' ' << extractAircraftId(header.aircraft_id);
    return line.str();
}

bool validateAircraftId(const std::string& aircraftId) {
    static const std::regex pattern("^AC-[0-9]{3}$");
    return std::regex_match(aircraftId, pattern);
}

bool sendPacket(SocketHandle socketHandle, const PacketHeader& header, const std::uint8_t* payload) {
    if (!sendAll(socketHandle, &header, sizeof(header))) {
        return false;
    }
    if (header.payload_size > 0) {
        return sendAll(socketHandle, payload, header.payload_size);
    }
    return true;
}

void generateWeatherMap(const std::string& path) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    constexpr int width = 1024;
    constexpr int height = 1024;
    constexpr int bytesPerPixel = 3;
    constexpr std::uint32_t pixelDataSize = width * height * bytesPerPixel;
    constexpr std::uint32_t fileSize = 54 + pixelDataSize;

    std::array<std::uint8_t, 54> header {};
    header[0] = 'B';
    header[1] = 'M';
    std::memcpy(&header[2], &fileSize, sizeof(fileSize));
    const std::uint32_t pixelOffset = 54;
    std::memcpy(&header[10], &pixelOffset, sizeof(pixelOffset));
    const std::uint32_t dibSize = 40;
    std::memcpy(&header[14], &dibSize, sizeof(dibSize));
    std::int32_t signedWidth = width;
    std::int32_t signedHeight = height;
    std::memcpy(&header[18], &signedWidth, sizeof(signedWidth));
    std::memcpy(&header[22], &signedHeight, sizeof(signedHeight));
    const std::uint16_t planes = 1;
    const std::uint16_t bitCount = 24;
    std::memcpy(&header[26], &planes, sizeof(planes));
    std::memcpy(&header[28], &bitCount, sizeof(bitCount));
    std::memcpy(&header[34], &pixelDataSize, sizeof(pixelDataSize));

    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));

    std::vector<std::uint8_t> row(static_cast<std::size_t>(width) * bytesPerPixel);
    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t offset = static_cast<std::size_t>(x) * bytesPerPixel;
            row[offset + 0] = static_cast<std::uint8_t>((x * 255) / width);
            row[offset + 1] = static_cast<std::uint8_t>((y * 255) / height);
            row[offset + 2] = 0;
        }
        out.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
    }
}

bool sendWeatherMap(
    SocketHandle clientSocket,
    Logger& logger,
    SharedServerState& sharedState,
    StateMachine& stateMachine,
    const std::string& aircraftId,
    std::uint32_t& sequenceCounter,
    std::uint32_t lastSequence) {
    if (stateMachine.getState() == StateMachine::State::TELEMETRY) {
        stateMachine.transition(StateMachine::State::CONNECTED);
    }
    if (!stateMachine.transition(StateMachine::State::LARGE_FILE_TRANSFER)) {
        logger.logFault("IllegalFileTransferTransition", stateMachine.stateToString(stateMachine.getState()), lastSequence);
        return false;
    }

    setDashboardConnection(sharedState, StateMachine::State::LARGE_FILE_TRANSFER, true, aircraftId);

    std::ifstream file(kWeatherMapPath, std::ios::binary | std::ios::ate);
    if (!file) {
        logger.logFault("WeatherMapMissing", stateMachine.stateToString(stateMachine.getState()), lastSequence);
        stateMachine.transition(StateMachine::State::FAULT);
        setDashboardConnection(sharedState, StateMachine::State::FAULT, true, aircraftId, "Weather map missing");
        return false;
    }

    const auto size = static_cast<std::uint32_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    LargeFilePayload payload;
    payload.data = static_cast<std::uint8_t*>(std::malloc(size));
    payload.size = size;
    if (!payload.data || !file.read(reinterpret_cast<char*>(payload.data), size)) {
        logger.logFault("WeatherMapReadFailure", stateMachine.stateToString(stateMachine.getState()), lastSequence);
        stateMachine.transition(StateMachine::State::FAULT);
        setDashboardConnection(sharedState, StateMachine::State::FAULT, true, aircraftId, "Unable to read weather map");
        return false;
    }

    PacketHeader header = makeHeader(
        PacketType::LARGE_FILE,
        aircraftId,
        sequenceCounter++,
        payload.size,
        computeChecksum(payload.data, payload.size));

    if (!sendAll(clientSocket, &header, sizeof(header))) {
        logger.logFault("FileHeaderSendFailure", stateMachine.stateToString(stateMachine.getState()), lastSequence);
        stateMachine.transition(StateMachine::State::FAULT);
        setDashboardConnection(sharedState, StateMachine::State::FAULT, true, aircraftId, "File transfer failed");
        return false;
    }

    for (std::uint32_t offset = 0; offset < payload.size; offset += static_cast<std::uint32_t>(kChunkSize)) {
        const std::uint32_t chunkSize =
            std::min<std::uint32_t>(static_cast<std::uint32_t>(kChunkSize), payload.size - offset);
        if (!sendAll(clientSocket, payload.data + offset, chunkSize)) {
            logger.logFault("FileChunkSendFailure", stateMachine.stateToString(stateMachine.getState()), lastSequence);
            stateMachine.transition(StateMachine::State::FAULT);
            setDashboardConnection(sharedState, StateMachine::State::FAULT, true, aircraftId, "File transfer interrupted");
            return false;
        }
    }

    logger.logPacket("TX", header);
    appendDashboardLog(sharedState, compactPacketLog("TX", header));

    stateMachine.transition(StateMachine::State::CONNECTED);
    setDashboardConnection(sharedState, StateMachine::State::CONNECTED, true, aircraftId);
    return true;
}

bool receivePacketPayload(SocketHandle clientSocket, const PacketHeader& header, std::vector<std::uint8_t>& payload) {
    payload.assign(header.payload_size, 0);
    if (header.payload_size == 0) {
        return true;
    }
    return recvAll(clientSocket, payload.data(), header.payload_size);
}

void handleFault(
    Logger& logger,
    SharedServerState& sharedState,
    StateMachine& stateMachine,
    const std::string& aircraftId,
    const std::string& cause,
    std::uint32_t sequenceNumber) {
    const StateMachine::State currentState = stateMachine.getState();
    if (currentState != StateMachine::State::FAULT) {
        stateMachine.transition(StateMachine::State::FAULT);
    }
    logger.logFault(cause, stateMachine.stateToString(currentState), sequenceNumber);
    setDashboardConnection(sharedState, StateMachine::State::FAULT, !aircraftId.empty(), aircraftId, cause);
    appendDashboardLog(sharedState, "[" + timeStampHmsUtc() + "] FAULT " + cause);
    stateMachine.transition(StateMachine::State::DISCONNECTED);
}

void serverThreadMain(
    SocketHandle listenSocket,
    std::atomic<bool>& running,
    Logger& logger,
    SharedServerState& sharedState,
    bool stopAfterSingleSession) {
    StateMachine stateMachine;
    std::uint32_t sequenceCounter = 1;

    while (running.load()) {
        sockaddr_in clientAddress {};
        SockLenType clientLength = static_cast<SockLenType>(sizeof(clientAddress));
        const SocketHandle clientSocket =
            accept(listenSocket, reinterpret_cast<sockaddr*>(&clientAddress), &clientLength);

        if (clientSocket == INVALID_SOCK) {
            if (running.load()) {
                logger.logInfo(socketErrorString("accept() failed"));
            }
            continue;
        }

        if (!stateMachine.transition(StateMachine::State::HANDSHAKE_PENDING)) {
            closeSocket(clientSocket);
            continue;
        }
        setDashboardConnection(sharedState, StateMachine::State::HANDSHAKE_PENDING, false, "N/A");

        PacketHeader header {};
        if (!recvAll(clientSocket, &header, sizeof(header))) {
            handleFault(logger, sharedState, stateMachine, std::string(), "HandshakeReceiveFailure", 0);
            closeSocket(clientSocket);
            continue;
        }

        logger.logPacket("RX", header);
        appendDashboardLog(sharedState, compactPacketLog("RX", header));

        const std::string aircraftId = extractAircraftId(header.aircraft_id);
        if (header.packet_type != PacketType::HANDSHAKE_REQUEST || header.payload_size != 0 ||
            !validateAircraftId(aircraftId)) {
            PacketHeader failHeader = makeHeader(PacketType::HANDSHAKE_FAIL, aircraftId, header.sequence_number, 0, 0);
            sendPacket(clientSocket, failHeader, nullptr);
            logger.logPacket("TX", failHeader);
            appendDashboardLog(sharedState, compactPacketLog("TX", failHeader));
            stateMachine.transition(StateMachine::State::DISCONNECTED);
            setDashboardConnection(sharedState, StateMachine::State::DISCONNECTED, false, "N/A", "Invalid handshake");
            closeSocket(clientSocket);
            continue;
        }

        PacketHeader ackHeader = makeHeader(PacketType::HANDSHAKE_ACK, aircraftId, header.sequence_number, 0, 0);
        if (!sendPacket(clientSocket, ackHeader, nullptr)) {
            handleFault(logger, sharedState, stateMachine, aircraftId, "HandshakeAckSendFailure", header.sequence_number);
            closeSocket(clientSocket);
            continue;
        }

        logger.logPacket("TX", ackHeader);
        appendDashboardLog(sharedState, compactPacketLog("TX", ackHeader));
        stateMachine.transition(StateMachine::State::CONNECTED);
        setDashboardConnection(sharedState, StateMachine::State::CONNECTED, true, aircraftId);

        bool connected = true;
        std::uint32_t lastSequence = header.sequence_number;
        auto lastPacketTime = std::chrono::steady_clock::now();

        while (running.load() && connected) {
            bool sendWeatherRequested = false;
            bool disconnectRequested = false;
            clearDashboardFlags(sharedState, sendWeatherRequested, disconnectRequested);

            if (disconnectRequested) {
                if (stateMachine.getState() == StateMachine::State::TELEMETRY) {
                    stateMachine.transition(StateMachine::State::CONNECTED);
                }
                PacketHeader disconnectHeader =
                    makeHeader(PacketType::DISCONNECT, aircraftId, sequenceCounter++, 0, 0);
                sendPacket(clientSocket, disconnectHeader, nullptr);
                logger.logPacket("TX", disconnectHeader);
                appendDashboardLog(sharedState, compactPacketLog("TX", disconnectHeader));
                stateMachine.transition(StateMachine::State::DISCONNECTED);
                setDashboardConnection(sharedState, StateMachine::State::DISCONNECTED, false, "N/A");
                connected = false;
                break;
            }

            if (sendWeatherRequested) {
                if (!sendWeatherMap(
                        clientSocket,
                        logger,
                        sharedState,
                        stateMachine,
                        aircraftId,
                        sequenceCounter,
                        lastSequence)) {
                    connected = false;
                    break;
                }
                lastPacketTime = std::chrono::steady_clock::now();
            }

            if (!waitForReadable(clientSocket, 250)) {
                const auto now = std::chrono::steady_clock::now();
                const auto timeoutSeconds =
                    stateMachine.getState() == StateMachine::State::TELEMETRY ? 3 : 5;
                if (std::chrono::duration_cast<std::chrono::seconds>(now - lastPacketTime).count() >= timeoutSeconds &&
                    stateMachine.getState() != StateMachine::State::DISCONNECTED) {
                    handleFault(logger, sharedState, stateMachine, aircraftId, "SocketTimeout", lastSequence);
                    connected = false;
                }
                continue;
            }

            if (!recvAll(clientSocket, &header, sizeof(header))) {
                handleFault(logger, sharedState, stateMachine, aircraftId, "ReceiveFailure", lastSequence);
                connected = false;
                break;
            }

            logger.logPacket("RX", header);
            appendDashboardLog(sharedState, compactPacketLog("RX", header));
            lastPacketTime = std::chrono::steady_clock::now();
            lastSequence = header.sequence_number;

            std::vector<std::uint8_t> payload;
            if (!receivePacketPayload(clientSocket, header, payload)) {
                handleFault(logger, sharedState, stateMachine, aircraftId, "PayloadReceiveFailure", lastSequence);
                connected = false;
                break;
            }

            if (computeChecksum(payload.data(), header.payload_size) != header.checksum) {
                handleFault(logger, sharedState, stateMachine, aircraftId, "ChecksumMismatch", lastSequence);
                connected = false;
                break;
            }

            switch (header.packet_type) {
                case PacketType::TELEMETRY: {
                    if (header.payload_size != sizeof(TelemetryPayload)) {
                        handleFault(logger, sharedState, stateMachine, aircraftId, "TelemetrySizeMismatch", lastSequence);
                        connected = false;
                        break;
                    }
                    if (stateMachine.getState() == StateMachine::State::CONNECTED) {
                        stateMachine.transition(StateMachine::State::TELEMETRY);
                    }
                    TelemetryPayload telemetry {};
                    std::memcpy(&telemetry, payload.data(), sizeof(telemetry));
                    updateTelemetry(sharedState, telemetry);
                    break;
                }
                case PacketType::LARGE_FILE: {
                    if (stateMachine.getState() == StateMachine::State::TELEMETRY) {
                        stateMachine.transition(StateMachine::State::CONNECTED);
                    }
                    if (!sendWeatherMap(
                            clientSocket,
                            logger,
                            sharedState,
                            stateMachine,
                            aircraftId,
                            sequenceCounter,
                            lastSequence)) {
                        connected = false;
                    }
                    break;
                }
                case PacketType::DISCONNECT:
                    if (stateMachine.getState() == StateMachine::State::TELEMETRY) {
                        stateMachine.transition(StateMachine::State::CONNECTED);
                    }
                    stateMachine.transition(StateMachine::State::DISCONNECTED);
                    setDashboardConnection(sharedState, StateMachine::State::DISCONNECTED, false, "N/A");
                    connected = false;
                    break;
                default:
                    handleFault(logger, sharedState, stateMachine, aircraftId, "UnexpectedPacketType", lastSequence);
                    connected = false;
                    break;
            }
        }

        shutdownSocket(clientSocket);
        closeSocket(clientSocket);
        if (stateMachine.getState() != StateMachine::State::DISCONNECTED) {
            stateMachine.transition(StateMachine::State::DISCONNECTED);
        }
        if (!dashboardShowsFault(sharedState)) {
            setDashboardConnection(sharedState, StateMachine::State::DISCONNECTED, false, "N/A");
        }

        if (stopAfterSingleSession) {
            running.store(false);
        }
    }
}

std::optional<ServerOptions> parseServerOptions(int argc, char* argv[]) {
    ServerOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--headless") {
            options.headless = true;
            continue;
        }

        try {
            const int parsedPort = std::stoi(arg);
            if (parsedPort < 1 || parsedPort > 65535) {
                std::cerr << "Invalid port. Please choose a value between 1 and 65535.\n";
                return std::nullopt;
            }
            options.listenPort = static_cast<std::uint16_t>(parsedPort);
        } catch (const std::exception&) {
            std::cerr << "Invalid argument. Usage: ./ground_server [port] [--headless]\n";
            return std::nullopt;
        }
    }

    return options;
}

} // namespace

int main(int argc, char* argv[]) {
    const std::optional<ServerOptions> parsedOptions = parseServerOptions(argc, argv);
    if (!parsedOptions.has_value()) {
        return 1;
    }
    const ServerOptions options = *parsedOptions;

    if (!std::filesystem::exists(kWeatherMapPath)) {
        generateWeatherMap(kWeatherMapPath);
    }

    initSockets();
    Logger logger("groundctrl");

    SocketHandle listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCK) {
        std::cerr << "Unable to create server socket.\n";
        cleanupSockets();
        return 1;
    }

    setReuseAddress(listenSocket);

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(options.listenPort);
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listenSocket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCK_ERR) {
        std::cerr << "Unable to bind server socket on port " << options.listenPort << ".\n";
        closeSocket(listenSocket);
        cleanupSockets();
        return 1;
    }

    if (listen(listenSocket, 4) == SOCK_ERR) {
        std::cerr << "Unable to listen on server socket on port " << options.listenPort << ".\n";
        closeSocket(listenSocket);
        cleanupSockets();
        return 1;
    }

    SharedServerState sharedState;
    std::atomic<bool> running {true};

    if (options.headless) {
        std::cout << "Ground server running in headless mode on port " << options.listenPort
                  << " for a single test session.\n";
        std::thread worker(
            serverThreadMain,
            listenSocket,
            std::ref(running),
            std::ref(logger),
            std::ref(sharedState),
            true);
        if (worker.joinable()) {
            worker.join();
        }
        shutdownSocket(listenSocket);
        closeSocket(listenSocket);
        cleanupSockets();
        return 0;
    }

    if (!glfwInit()) {
        std::cerr << "Unable to initialize GLFW.\n";
        closeSocket(listenSocket);
        cleanupSockets();
        return 1;
    }

    const char* glslVersion = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow* window = glfwCreateWindow(980, 700, "Ground Control - CSCN74000", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Unable to create GLFW window.\n";
        glfwTerminate();
        closeSocket(listenSocket);
        cleanupSockets();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);

    std::thread worker(
        serverThreadMain,
        listenSocket,
        std::ref(running),
        std::ref(logger),
        std::ref(sharedState),
        false);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        {
            std::lock_guard<std::mutex> lock(sharedState.mutex);
            renderDashboard(sharedState.dashboard);
        }

        ImGui::Render();
        int displayWidth = 0;
        int displayHeight = 0;
        glfwGetFramebufferSize(window, &displayWidth, &displayHeight);
        glViewport(0, 0, displayWidth, displayHeight);
        glClearColor(0.09f, 0.11f, 0.14f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    running.store(false);
    shutdownSocket(listenSocket);
    closeSocket(listenSocket);
    if (worker.joinable()) {
        worker.join();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    cleanupSockets();
    return 0;
}
