#include <iostream>
#include <iomanip>
#include <string>
#include <winsock2.h>
#include "ServerState.h"

#pragma comment(lib, "ws2_32.lib")

using namespace std;

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
    WSADATA wsaData;
    SOCKET listenSocket = INVALID_SOCKET;
    SOCKET clientSocket = INVALID_SOCKET;
    struct sockaddr_in serverAddr;

    // current server state is disconnected
    ServerState currentState = STATE_DISCONNECTED;

    ServerDashboard ui;
    drawServerDashboard(ui);

    // initializing socket
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    // creating socket and binding
    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(5000);

    bind(listenSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(listenSocket, SOMAXCONN);

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
        // changing the server state
        currentState = STATE_HANDSHAKE_PENDING;

        ui.connectionStatus = "CLIENT CONNECTED";
        ui.operatorState = "HANDSHAKE_PENDING";
        ui.selectedAircraft = "AC-101";
        ui.lastEvent = "Aircraft connected";
        drawServerDashboard(ui);

        // for just testing
        char buffer[512];
        int bytesReceived = recv(clientSocket, buffer, 512, 0);

        if (bytesReceived > 0)
        {
            // adding null terminator
            buffer[bytesReceived] = '\0';

            ui.operatorState = "MESSAGE_RECEIVED";
            ui.lastEvent = "Message received from aircraft";
            ui.latitude = 43.6655;
            ui.longitude = -79.4012;
            ui.altitude = 32150.0;
            ui.speed = 447.0;
            ui.heading = 80.0;
            drawServerDashboard(ui);

            cout << "Message received from Aircraft: " << buffer << endl;
        }
    }

    // Cleanup
    closesocket(clientSocket);
    closesocket(listenSocket);
    WSACleanup();

    ui.serverStatus = "OFFLINE";
    ui.connectionStatus = "NO CLIENT";
    ui.operatorState = "COMPLETE";
    ui.lastEvent = "Server shutdown";
    drawServerDashboard(ui);

    return 0;
}