#include <iostream>
#include <iomanip>
#include <memory>
#include <string>
#include <winsock2.h>

#include "../../../Shared/PacketLogger.h"
#include "../../../Shared/SocketUtils.h"
#include "PacketHeader.h"
#include "ServerState.h"
#include "TelemetryPayload.h"
#include "ServerDashboard.h"
#include "ServerDashboardHelpers.h"
#include "CommandGate.h"

#pragma comment(lib, "ws2_32.lib")

using namespace std;

namespace
{
    const unsigned short kServerPort = 5000;
    const char* const kServerLogFile = "server_packet_log.txt";

    const char* packetTypeToString(PacketType packetType)
    {
        switch (packetType)
        {
        case PacketType::HandshakeRequest:
            return "HANDSHAKE_REQUEST";
        case PacketType::HandshakeAck:
            return "HANDSHAKE_ACK";
        case PacketType::Telemetry:
            return "TELEMETRY";
        case PacketType::LargeFile:
            return "LARGE_FILE";
        case PacketType::Command:
            return "COMMAND";
        default:
            return "UNKNOWN";
        }
    }

    bool sendPacket(SOCKET socketHandle, const PacketHeader& header, const void* payload)
    {
        if (!sendAll(socketHandle, &header, static_cast<int>(sizeof(header))))
        {
            return false;
        }

        return sendBuffer(socketHandle, payload, header.payload_size);
    }

    bool receivePacket(SOCKET socketHandle, PacketHeader& header, unique_ptr<uint8_t[]>& payloadBuffer)
    {
        if (!recvAll(socketHandle, &header, static_cast<int>(sizeof(header))))
        {
            return false;
        }

        return receiveDynamicBuffer(socketHandle, header.payload_size, payloadBuffer);
    }
}

void clearScreen()
{
    system("cls");
}

void printDivider(char ch = '=')
{
    cout << string(68, ch) << '\n';
}

void drawServerDashboard(const ServerDashboard& s)
{
    clearScreen();

    printDivider();
    cout << "                 GROUND CONTROL SERVER DASHBOARD\n";
    printDivider();

    cout << left;
    cout << setw(24) << "Server Status" << ": " << s.serverStatus << '\n';
    cout << setw(24) << "Connection Status" << ": " << s.connectionStatus << '\n';
    cout << setw(24) << "Telemetry Alert" << ": " << s.telemetryAlert << '\n';
    cout << setw(24) << "Operator State" << ": " << s.operatorState << '\n';
    cout << setw(24) << "Listening Port" << ": " << s.listeningPort << '\n';
    cout << setw(24) << "Selected Aircraft" << ": " << s.selectedAircraft << '\n';
    cout << setw(24) << "Last Event" << ": " << s.lastEvent << '\n';

    printDivider('-');
    cout << "Latest Packet Data\n";
    printDivider('-');
    cout << setw(24) << "Packet Type" << ": " << s.packetType << '\n';
    cout << setw(24) << "Aircraft ID" << ": " << s.aircraftId << '\n';
    cout << setw(24) << "Sequence Number" << ": " << s.sequenceNumber << '\n';
    cout << setw(24) << "Payload Size" << ": " << s.payloadSize << '\n';

    printDivider('-');
    cout << "Latest Aircraft Telemetry\n";
    printDivider('-');

    cout << fixed << setprecision(4);
    cout << setw(24) << "Latitude" << ": " << s.latitude << '\n';
    cout << setw(24) << "Longitude" << ": " << s.longitude << '\n';

    cout << setprecision(1);
    cout << setw(24) << "Altitude (ft)" << ": " << s.altitude << '\n';
    cout << setw(24) << "Speed (knots)" << ": " << s.speed << '\n';
    cout << setw(24) << "Heading (deg)" << ": " << s.heading << '\n';

    printDivider('-');
    cout << "ATC Operator Commands\n";
    printDivider('-');
    cout << "1. Start Server\n";
    cout << "2. Accept Client Connection\n";
    cout << "3. Register Handshake ACK\n";
    cout << "4. Receive Telemetry Update\n";
    cout << "5. Dispatch Weather Map\n";
    cout << "0. Exit\n";

    printDivider();
}

bool tryDispatchCommand(ServerState& currentState, ServerDashboard& ui)
{
    if (!canDispatchCommand(currentState))
    {
        const std::string reason = commandGateReason(currentState);
        currentState = STATE_FAULT;
        ui.connectionStatus = "FAULT";
        ui.operatorState = "FAULT";
        ui.telemetryAlert = "FAULT";
        ui.lastEvent = reason;
        return false;
    }

    ui.lastEvent = "Command packet dispatched";
    return true;
}

int main()
{
    WSADATA wsaData = {};
    SOCKET listenSocket = INVALID_SOCKET;
    SOCKET clientSocket = INVALID_SOCKET;
    struct sockaddr_in serverAddr = {};
    PacketLogger packetLogger(kServerLogFile);

    ServerState currentState = STATE_DISCONNECTED;

    ServerDashboard ui;
    TelemetryMonitor telemetryMonitor;

    drawServerDashboard(ui);

    if (!packetLogger.isOpen())
    {
        ui.operatorState = "LOG_ERROR";
        ui.lastEvent = "Packet log creation failed";
        drawServerDashboard(ui);
        cout << "Unable to create packet log file." << endl;
        return 1;
    }

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        ui.operatorState = "STARTUP_ERROR";
        ui.lastEvent = "Winsock initialization failed";
        drawServerDashboard(ui);
        cout << "Winsock initialization failed!" << endl;
        return 1;
    }

    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listenSocket == INVALID_SOCKET)
    {
        ui.operatorState = "SOCKET_ERROR";
        ui.lastEvent = "Socket creation failed";
        drawServerDashboard(ui);
        cout << "Server socket creation failed!" << endl;
        WSACleanup();
        return 1;
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(kServerPort);

    if (bind(listenSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        ui.operatorState = "BIND_ERROR";
        ui.lastEvent = "Bind failed on port 5000";
        drawServerDashboard(ui);
        cout << "Bind failed on port 5000!" << endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
    {
        ui.operatorState = "LISTEN_ERROR";
        ui.lastEvent = "Listen failed";
        drawServerDashboard(ui);
        cout << "Listen failed!" << endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    ui.serverStatus = "ONLINE";
    ui.operatorState = "WAITING";
    ui.lastEvent = "Listening for aircraft";
    drawServerDashboard(ui);

    cout << "current server state is STATE_DISCONNECTED" << endl;
    cout << "waiting for aircraft on port " << kServerPort << endl;

    clientSocket = accept(listenSocket, NULL, NULL);

    if (clientSocket != INVALID_SOCKET)
    {
        currentState = STATE_HANDSHAKE_PENDING;
        ui.connectionStatus = "CLIENT CONNECTED";
        ui.operatorState = "HANDSHAKE_PENDING";
        ui.lastEvent = "TCP connection accepted";
        drawServerDashboard(ui);

        PacketHeader handshakeRequest = {};
        unique_ptr<uint8_t[]> handshakePayload;

        if (!receivePacket(clientSocket, handshakeRequest, handshakePayload))
        {
            ui.connectionStatus = "NO CLIENT";
            ui.operatorState = "DISCONNECTED";
            ui.lastEvent = "Handshake request not received";
            currentState = STATE_DISCONNECTED;
            drawServerDashboard(ui);
        }
        else if (handshakeRequest.packet_type != PacketType::HandshakeRequest || handshakeRequest.payload_size != 0)
        {
            ui.connectionStatus = "NO CLIENT";
            ui.operatorState = "DISCONNECTED";
            ui.lastEvent = "Invalid handshake request";
            currentState = STATE_DISCONNECTED;
            drawServerDashboard(ui);
        }
        else
        {
            packetLogger.logPacket("RX",
                packetTypeToString(handshakeRequest.packet_type),
                handshakeRequest.aircraft_id,
                handshakeRequest.sequence_number,
                handshakeRequest.payload_size);

            applyHeaderToDashboard(ui, handshakeRequest);
            ui.selectedAircraft = "AC-" + to_string(handshakeRequest.aircraft_id);
            ui.lastEvent = "Handshake request received";
            drawServerDashboard(ui);

            PacketHeader handshakeAck = {};
            handshakeAck.packet_type = PacketType::HandshakeAck;
            handshakeAck.aircraft_id = handshakeRequest.aircraft_id;
            handshakeAck.sequence_number = handshakeRequest.sequence_number;
            handshakeAck.payload_size = 0;

            if (!sendPacket(clientSocket, handshakeAck, NULL))
            {
                ui.connectionStatus = "NO CLIENT";
                ui.operatorState = "FAULT";
                ui.lastEvent = "Handshake ACK send failed";
                currentState = STATE_FAULT;
                drawServerDashboard(ui);
            }
            else
            {
                packetLogger.logPacket("TX",
                    packetTypeToString(handshakeAck.packet_type),
                    handshakeAck.aircraft_id,
                    handshakeAck.sequence_number,
                    handshakeAck.payload_size);

                applyHeaderToDashboard(ui, handshakeAck);

                currentState = STATE_CONNECTED;
                ui.operatorState = "CONNECTED";
                ui.selectedAircraft = "AC-" + to_string(handshakeAck.aircraft_id);
                ui.lastEvent = "Handshake ACK sent";
                drawServerDashboard(ui);

                cout << "Handshake completed on port " << kServerPort << endl;

                bool running = true;
                while (running)
                {
                    refreshTelemetryAlert(ui, telemetryMonitor);
                    drawServerDashboard(ui);

                    int choice = -1;
                    cin >> choice;

                    if (cin.fail())
                    {
                        cin.clear();
                        cin.ignore(10000, '\n');
                        continue;
                    }

                    cin.ignore(10000, '\n');

                    switch (choice)
                    {
                    case 1:
                        ui.lastEvent = "Server already running";
                        break;

                    case 2:
                        ui.lastEvent = "Client already connected";
                        break;

                    case 3:
                        ui.lastEvent = "Handshake already completed";
                        break;

                    case 4:
                        simulateTelemetryUpdate(ui, telemetryMonitor);
                        currentState = STATE_TELEMETRY;
                        break;

                    case 5:
                        if (tryDispatchCommand(currentState, ui))
                        {
                            ui.lastEvent = "Weather map dispatch placeholder";
                            currentState = STATE_LARGE_FILE_TRANSFER;
                            ui.operatorState = "LARGE_FILE_TRANSFER";
                        }
                        break;

                    case 0:
                        running = false;
                        break;

                    default:
                        ui.lastEvent = "Invalid menu option";
                        break;
                    }
                }
            }
        }

        handshakePayload.reset();
    }

    if (clientSocket != INVALID_SOCKET)
    {
        closesocket(clientSocket);
    }

    if (listenSocket != INVALID_SOCKET)
    {
        closesocket(listenSocket);
    }

    WSACleanup();

    ui.serverStatus = "OFFLINE";
    ui.connectionStatus = "NO CLIENT";
    ui.operatorState = "COMPLETE";
    ui.lastEvent = "Server shutdown";
    drawServerDashboard(ui);

    return 0;
}