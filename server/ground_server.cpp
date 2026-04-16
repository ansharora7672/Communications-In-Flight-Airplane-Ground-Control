#include "ground_server.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
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

#include "client_session.h"
#include "packet.h"
#include "server_utils.h"
#include "weather_map.h"

namespace server {
namespace {

constexpr std::size_t kChunkSize = 4096;
const std::string kImguiIniPath = "runtime/ui/imgui.ini";

using SessionMap = std::map<SocketHandle, ClientSession>;

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

std::string compactPacketLog(const std::string& direction, const PacketHeader& header) {
    std::ostringstream line;
    line << "[" << timeStampHmsUtc() << "] "
         << direction << ' '
         << packetTypeToString(header.packet_type)
         << " Seq=" << header.sequence_number
         << ' ' << extractAircraftId(header.aircraft_id);
    return line.str();
}

void updateDashboardAircraft(
    SharedServerState& sharedState,
    const ClientSession& session,
    const std::string& alertMessage = std::string()) {
    if (session.aircraftId().empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(sharedState.mutex);
    auto& aircraft = sharedState.dashboard.aircraft[session.aircraftId()];
    aircraft.aircraftId = session.aircraftId();
    aircraft.state = session.stateMachine().getState();
    aircraft.connected =
        session.stateMachine().getState() != StateMachine::State::DISCONNECTED &&
        session.stateMachine().getState() != StateMachine::State::FAULT;
    aircraft.telemetry = session.telemetry();
    aircraft.telemetryValid = session.hasTelemetry();
    aircraft.alertMessage = alertMessage;

    if (sharedState.dashboard.selectedAircraftId.empty()) {
        sharedState.dashboard.selectedAircraftId = session.aircraftId();
    }
}

std::string consumeWeatherRequest(SharedServerState& sharedState) {
    std::lock_guard<std::mutex> lock(sharedState.mutex);
    std::string aircraftId = sharedState.dashboard.weatherRequestAircraftId;
    sharedState.dashboard.weatherRequestAircraftId.clear();
    return aircraftId;
}

std::string consumeDisconnectRequest(SharedServerState& sharedState) {
    std::lock_guard<std::mutex> lock(sharedState.mutex);
    std::string aircraftId = sharedState.dashboard.disconnectRequestAircraftId;
    sharedState.dashboard.disconnectRequestAircraftId.clear();
    return aircraftId;
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

bool aircraftIdInUse(const SessionMap& sessions, SocketHandle currentSocket, const std::string& aircraftId) {
    for (const auto& [socketHandle, session] : sessions) {
        if (socketHandle == currentSocket) {
            continue;
        }
        if (session.aircraftId() == aircraftId &&
            session.stateMachine().getState() != StateMachine::State::DISCONNECTED) {
            return true;
        }
    }
    return false;
}

void handleFault(
    ClientSession& session,
    Logger& logger,
    SharedServerState& sharedState,
    const std::string& cause) {
    const StateMachine::State currentState = session.stateMachine().getState();
    if (currentState != StateMachine::State::FAULT) {
        session.stateMachine().transition(StateMachine::State::FAULT);
    }
    logger.logFault(cause, session.stateMachine().stateToString(currentState), session.lastClientSequence());
    appendDashboardLog(
        sharedState,
        "[" + timeStampHmsUtc() + "] FAULT " +
            (session.aircraftId().empty() ? std::string("UNKNOWN") : session.aircraftId() + " ") + cause);
    updateDashboardAircraft(sharedState, session, cause);
    session.stateMachine().transition(StateMachine::State::DISCONNECTED);
    updateDashboardAircraft(sharedState, session);
}

WeatherMapContext weatherMapContextFor(const ClientSession& session) {
    WeatherMapContext context;
    context.aircraftId = session.aircraftId();
    context.telemetry = session.telemetry();
    context.telemetryValid = session.hasTelemetry();
    return context;
}

bool sendWeatherMap(ClientSession& session, Logger& logger, SharedServerState& sharedState) {
    if (session.stateMachine().getState() == StateMachine::State::TELEMETRY) {
        session.stateMachine().transition(StateMachine::State::CONNECTED);
    }
    if (!session.stateMachine().transition(StateMachine::State::LARGE_FILE_TRANSFER)) {
        handleFault(session, logger, sharedState, "IllegalFileTransferTransition");
        return false;
    }
    updateDashboardAircraft(sharedState, session);

    const std::uint32_t transferSequence = session.serverSequence();
    const std::string weatherMapPath = generatedWeatherMapPathFor(session.aircraftId(), transferSequence);
    generateWeatherMap(weatherMapPath, weatherMapContextFor(session), transferSequence);

    std::ifstream file(weatherMapPath, std::ios::binary | std::ios::ate);
    if (!file) {
        handleFault(session, logger, sharedState, "WeatherMapMissing");
        return false;
    }

    const auto size = static_cast<std::uint32_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    LargeFilePayload payload;
    payload.data = static_cast<std::uint8_t*>(std::malloc(size));
    payload.size = size;
    if (!payload.data || !file.read(reinterpret_cast<char*>(payload.data), size)) {
        handleFault(session, logger, sharedState, "WeatherMapReadFailure");
        return false;
    }

    PacketHeader header = makeHeader(
        PacketType::LARGE_FILE,
        session.aircraftId(),
        transferSequence,
        payload.size,
        computeChecksum(payload.data, payload.size));
    session.consumeServerSequence();

    if (!sendAll(session.socket(), &header, sizeof(header))) {
        handleFault(session, logger, sharedState, "FileHeaderSendFailure");
        return false;
    }

    for (std::uint32_t offset = 0; offset < payload.size; offset += static_cast<std::uint32_t>(kChunkSize)) {
        const std::uint32_t chunkSize =
            (std::min<std::uint32_t>)(static_cast<std::uint32_t>(kChunkSize), payload.size - offset);
        if (!sendAll(session.socket(), payload.data + offset, chunkSize)) {
            handleFault(session, logger, sharedState, "FileChunkSendFailure");
            return false;
        }
    }

    logger.logPacket("TX", header);
    logger.logInfo("Generated weather map: " + weatherMapPath);
    appendDashboardLog(sharedState, compactPacketLog("TX", header));
    appendDashboardLog(
        sharedState,
        "[" + timeStampHmsUtc() + "] MAP " + session.aircraftId() + " " +
            std::filesystem::path(weatherMapPath).filename().string());
    session.stateMachine().transition(StateMachine::State::CONNECTED);
    updateDashboardAircraft(sharedState, session);
    return true;
}

bool receivePacketPayload(SocketHandle clientSocket, const PacketHeader& header, std::vector<std::uint8_t>& payload) {
    payload.assign(header.payload_size, 0);
    if (header.payload_size == 0) {
        return true;
    }
    return recvAll(clientSocket, payload.data(), header.payload_size);
}

SessionMap::iterator findSessionByAircraftId(SessionMap& sessions, const std::string& aircraftId) {
    for (auto it = sessions.begin(); it != sessions.end(); ++it) {
        if (it->second.aircraftId() == aircraftId) {
            return it;
        }
    }
    return sessions.end();
}

bool processHandshake(
    SessionMap& sessions,
    SessionMap::iterator sessionIt,
    const PacketHeader& header,
    Logger& logger,
    SharedServerState& sharedState) {
    ClientSession& session = sessionIt->second;
    const std::string aircraftId = extractAircraftId(header.aircraft_id);

    const bool validRequest =
        header.packet_type == PacketType::HANDSHAKE_REQUEST &&
        header.payload_size == 0 &&
        validateAircraftId(aircraftId) &&
        !aircraftIdInUse(sessions, session.socket(), aircraftId);

    if (!validRequest) {
        PacketHeader failHeader = makeHeader(PacketType::HANDSHAKE_FAIL, aircraftId, header.sequence_number, 0, 0);
        sendPacket(session.socket(), failHeader, nullptr);
        logger.logPacket("TX", failHeader);
        appendDashboardLog(sharedState, compactPacketLog("TX", failHeader));
        return false;
    }

    session.setAircraftId(aircraftId);
    session.markPacketReceived(header.sequence_number);

    PacketHeader ackHeader = makeHeader(PacketType::HANDSHAKE_ACK, aircraftId, header.sequence_number, 0, 0);
    if (!sendPacket(session.socket(), ackHeader, nullptr)) {
        handleFault(session, logger, sharedState, "HandshakeAckSendFailure");
        return false;
    }

    logger.logPacket("TX", ackHeader);
    appendDashboardLog(sharedState, compactPacketLog("TX", ackHeader));
    session.stateMachine().transition(StateMachine::State::CONNECTED);
    updateDashboardAircraft(sharedState, session);
    return true;
}

bool processReadableSession(
    SessionMap& sessions,
    SessionMap::iterator sessionIt,
    Logger& logger,
    SharedServerState& sharedState) {
    ClientSession& session = sessionIt->second;

    PacketHeader header {};
    if (!recvAll(session.socket(), &header, sizeof(header))) {
        handleFault(
            session,
            logger,
            sharedState,
            session.stateMachine().getState() == StateMachine::State::HANDSHAKE_PENDING ? "HandshakeReceiveFailure"
                                                                                       : "ReceiveFailure");
        return false;
    }

    logger.logPacket("RX", header);
    appendDashboardLog(sharedState, compactPacketLog("RX", header));

    if (session.stateMachine().getState() == StateMachine::State::HANDSHAKE_PENDING) {
        return processHandshake(sessions, sessionIt, header, logger, sharedState);
    }

    session.markPacketReceived(header.sequence_number);

    std::vector<std::uint8_t> payload;
    if (!receivePacketPayload(session.socket(), header, payload)) {
        handleFault(session, logger, sharedState, "PayloadReceiveFailure");
        return false;
    }

    if (computeChecksum(payload.data(), header.payload_size) != header.checksum) {
        handleFault(session, logger, sharedState, "ChecksumMismatch");
        return false;
    }

    switch (header.packet_type) {
        case PacketType::TELEMETRY: {
            if (header.payload_size != sizeof(TelemetryPayload)) {
                handleFault(session, logger, sharedState, "TelemetrySizeMismatch");
                return false;
            }
            if (session.stateMachine().getState() == StateMachine::State::CONNECTED) {
                session.stateMachine().transition(StateMachine::State::TELEMETRY);
            }
            if (session.stateMachine().getState() != StateMachine::State::TELEMETRY) {
                handleFault(session, logger, sharedState, "IllegalTelemetryState");
                return false;
            }
            TelemetryPayload telemetry {};
            std::memcpy(&telemetry, payload.data(), sizeof(telemetry));
            session.updateTelemetry(telemetry);
            updateDashboardAircraft(sharedState, session);
            return true;
        }
        case PacketType::LARGE_FILE:
            if (header.payload_size != 0) {
                handleFault(session, logger, sharedState, "LargeFileRequestPayloadUnexpected");
                return false;
            }
            return sendWeatherMap(session, logger, sharedState);
        case PacketType::DISCONNECT:
            if (session.stateMachine().getState() == StateMachine::State::TELEMETRY) {
                session.stateMachine().transition(StateMachine::State::CONNECTED);
            }
            session.stateMachine().transition(StateMachine::State::DISCONNECTED);
            updateDashboardAircraft(sharedState, session);
            return false;
        default:
            handleFault(session, logger, sharedState, "UnexpectedPacketType");
            return false;
    }
}

void removeSession(SessionMap& sessions, SocketHandle socketHandle) {
    const auto sessionIt = sessions.find(socketHandle);
    if (sessionIt == sessions.end()) {
        return;
    }

    sessionIt->second.close();
    sessions.erase(sessionIt);
}

void serverThreadMain(
    SocketHandle listenSocket,
    std::atomic<bool>& running,
    Logger& logger,
    SharedServerState& sharedState) {
    SessionMap sessions;

    while (running.load()) {
        const std::string weatherRequestAircraftId = consumeWeatherRequest(sharedState);
        if (!weatherRequestAircraftId.empty()) {
            const auto sessionIt = findSessionByAircraftId(sessions, weatherRequestAircraftId);
            if (sessionIt != sessions.end()) {
                if (!sendWeatherMap(sessionIt->second, logger, sharedState)) {
                    removeSession(sessions, sessionIt->first);
                    continue;
                }
            }
        }

        const std::string disconnectRequestAircraftId = consumeDisconnectRequest(sharedState);
        if (!disconnectRequestAircraftId.empty()) {
            const auto sessionIt = findSessionByAircraftId(sessions, disconnectRequestAircraftId);
            if (sessionIt != sessions.end()) {
                ClientSession& session = sessionIt->second;
                if (session.stateMachine().getState() == StateMachine::State::TELEMETRY) {
                    session.stateMachine().transition(StateMachine::State::CONNECTED);
                }
                PacketHeader disconnectHeader =
                    makeHeader(PacketType::DISCONNECT, session.aircraftId(), session.consumeServerSequence(), 0, 0);
                if (sendPacket(session.socket(), disconnectHeader, nullptr)) {
                    logger.logPacket("TX", disconnectHeader);
                    appendDashboardLog(sharedState, compactPacketLog("TX", disconnectHeader));
                    session.stateMachine().transition(StateMachine::State::DISCONNECTED);
                    updateDashboardAircraft(sharedState, session);
                } else {
                    handleFault(session, logger, sharedState, "DisconnectSendFailure");
                }
                removeSession(sessions, sessionIt->first);
                continue;
            }
        }

        const auto now = std::chrono::steady_clock::now();
        std::vector<SocketHandle> timedOutSessions;
        for (const auto& [socketHandle, session] : sessions) {
            if (session.timedOut(now)) {
                timedOutSessions.push_back(socketHandle);
            }
        }
        for (SocketHandle socketHandle : timedOutSessions) {
            auto sessionIt = sessions.find(socketHandle);
            if (sessionIt == sessions.end()) {
                continue;
            }
            handleFault(sessionIt->second, logger, sharedState, "SocketTimeout");
            removeSession(sessions, socketHandle);
        }
        if (!running.load()) {
            break;
        }

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(listenSocket, &readSet);

        SocketHandle maxSocket = listenSocket;
        for (const auto& [socketHandle, session] : sessions) {
            if (session.socket() != INVALID_SOCK) {
                FD_SET(session.socket(), &readSet);
                if (session.socket() > maxSocket) {
                    maxSocket = session.socket();
                }
            }
        }

        timeval timeout {};
        timeout.tv_sec = 0;
        timeout.tv_usec = 250000;

#ifdef _WIN32
        const int selectResult = select(0, &readSet, nullptr, nullptr, &timeout);
#else
        const int selectResult = select(maxSocket + 1, &readSet, nullptr, nullptr, &timeout);
#endif
        if (selectResult < 0) {
            if (running.load()) {
                logger.logInfo(socketErrorString("select() failed"));
            }
            continue;
        }
        if (selectResult == 0) {
            continue;
        }

        if (FD_ISSET(listenSocket, &readSet)) {
            sockaddr_in clientAddress {};
            SockLenType clientLength = static_cast<SockLenType>(sizeof(clientAddress));
            const SocketHandle clientSocket =
                accept(listenSocket, reinterpret_cast<sockaddr*>(&clientAddress), &clientLength);
            if (clientSocket != INVALID_SOCK) {
                ClientSession session(clientSocket);
                session.touch();
                session.stateMachine().transition(StateMachine::State::HANDSHAKE_PENDING);
                sessions.emplace(clientSocket, std::move(session));
            } else if (running.load()) {
                logger.logInfo(socketErrorString("accept() failed"));
            }
        }

        std::vector<SocketHandle> readySessions;
        for (const auto& [socketHandle, session] : sessions) {
            if (FD_ISSET(socketHandle, &readSet)) {
                readySessions.push_back(socketHandle);
            }
        }

        for (SocketHandle socketHandle : readySessions) {
            auto sessionIt = sessions.find(socketHandle);
            if (sessionIt == sessions.end()) {
                continue;
            }

            if (!processReadableSession(sessions, sessionIt, logger, sharedState)) {
                removeSession(sessions, socketHandle);
                if (!running.load()) {
                    break;
                }
            }
        }
    }

    for (auto& [socketHandle, session] : sessions) {
        (void)socketHandle;
        session.close();
    }
}

} // namespace

GroundServer::GroundServer(const ServerOptions& options) : options(options) {}

GroundServer::~GroundServer() {
    running.store(false);
    closeListenSocket();
}

int GroundServer::run() {
    if (!openListenSocket()) {
        return 1;
    }

    if (options.headless) {
        return runHeadless();
    }
    return runDashboard();
}

bool GroundServer::openListenSocket() {
    listenSocket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCK) {
        std::cerr << "Unable to create server socket.\n";
        return false;
    }

    setReuseAddress(listenSocket);

    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(options.listenPort);
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listenSocket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCK_ERR) {
        std::cerr << "Unable to bind server socket on port " << options.listenPort << ".\n";
        closeListenSocket();
        return false;
    }

    if (listen(listenSocket, 8) == SOCK_ERR) {
        std::cerr << "Unable to listen on server socket on port " << options.listenPort << ".\n";
        closeListenSocket();
        return false;
    }

    return true;
}

int GroundServer::runHeadless() {
    std::cout << "Ground server running in headless mode on port " << options.listenPort << ".\n";
    std::thread worker(serverThreadMain, listenSocket, std::ref(running), std::ref(logger), std::ref(sharedState));
    if (worker.joinable()) {
        worker.join();
    }
    closeListenSocket();
    return 0;
}

int GroundServer::runDashboard() {
    if (!glfwInit()) {
        std::cerr << "Unable to initialize GLFW.\n";
        closeListenSocket();
        return 1;
    }

    const char* glslVersion = "#version 130";
#ifdef __APPLE__
    glslVersion = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#else
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif
    GLFWwindow* window = glfwCreateWindow(1080, 760, "Ground Control - CSCN74000", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Unable to create GLFW window.\n";
        glfwTerminate();
        closeListenSocket();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    std::filesystem::create_directories(std::filesystem::path(kImguiIniPath).parent_path());
    ImGui::GetIO().IniFilename = kImguiIniPath.c_str();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);

    std::thread worker(serverThreadMain, listenSocket, std::ref(running), std::ref(logger), std::ref(sharedState));

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
    closeListenSocket();
    if (worker.joinable()) {
        worker.join();
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

void GroundServer::closeListenSocket() {
    if (listenSocket != INVALID_SOCK) {
        closeSocket(listenSocket);
        listenSocket = INVALID_SOCK;
    }
}

} // namespace server
