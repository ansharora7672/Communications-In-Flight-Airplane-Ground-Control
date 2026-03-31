#include <iostream>
#include <iomanip>
#include <memory>
#include <string>
#include <winsock2.h>

#include "../../../Shared/SocketUtils.h"
#include "PacketHeader.h"
#include "ServerState.h"

#pragma comment(lib, "ws2_32.lib")

using namespace std;

namespace
{
    const unsigned short kServerPort = 5000;

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

struct ServerDashboard
{
    string serverStatus = "OFFLINE";
    string connectionStatus = "NO CLIENT";
    string telemetryAlert = "STABLE";
    string operatorState = "DISCONNECTED";
    string selectedAircraft = "NONE";
    string lastEvent = "SERVER NOT STARTED";
    int listeningPort = 5000;

    double latitude = 0.0;
    double longitude = 0.0;
    double altitude = 0.0;
    double speed = 0.0;
    double heading = 0.0;
};

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

int main()
{
    WSADATA wsaData = {};
    SOCKET listenSocket = INVALID_SOCKET;
    SOCKET clientSocket = INVALID_SOCKET;
    struct sockaddr_in serverAddr = {};

    // current server state is disconnected
    ServerState currentState = STATE_DISCONNECTED;

    ServerDashboard ui;
    drawServerDashboard(ui);

    // initializing socket
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        ui.operatorState = "STARTUP_ERROR";
        ui.lastEvent = "Winsock initialization failed";
        drawServerDashboard(ui);
        cout << "Winsock initialization failed!" << endl;
        return 1;
    }

    // creating socket and binding
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
    cout << "waiting for aircraft on port 5000" << endl;

    // listen for 1 at a time as of now
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
                currentState = STATE_CONNECTED;
                ui.operatorState = "CONNECTED";
                ui.selectedAircraft = "AC-101";
                ui.lastEvent = "Handshake ACK sent";
                drawServerDashboard(ui);
                cout << "Handshake completed on port " << kServerPort << endl;
            }
        }

        handshakePayload.reset();
    }

    // Cleanup
    if (clientSocket != INVALID_SOCKET)
    {
        closesocket(clientSocket);
    }
    closesocket(listenSocket);
    WSACleanup();

    ui.serverStatus = "OFFLINE";
    ui.connectionStatus = "NO CLIENT";
    ui.operatorState = "COMPLETE";
    ui.lastEvent = "Server shutdown";
    drawServerDashboard(ui);

    return 0;
}
