#include <iostream>
#include <iomanip>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

using namespace std;

namespace
{
    const char* const kServerIpAddress = "127.0.0.1";
    const unsigned short kServerPort = 5000;
}

struct ClientDashboard
{
    string connectionStatus = "DISCONNECTED";
    string telemetryHealth = "NORMAL";
    string aircraftId = "AC-101";
    string clientState = "IDLE";
    string lastPacket = "NONE";
    string weatherMapStatus = "NOT REQUESTED";

    double latitude = 43.6532;
    double longitude = -79.3832;
    double altitude = 32000.0;
    double speed = 450.0;
    double heading = 78.0;
};

void clearScreen()
{
    system("cls");
}

void printDivider(char ch = '=')
{
    cout << string(64, ch) << '\n';
}

void drawClientDashboard(const ClientDashboard& s)
{
    clearScreen();

    printDivider();
    cout << "                   AIRCRAFT CLIENT CONSOLE\n";
    printDivider();

    cout << left;
    cout << setw(22) << "Connection Status" << ": " << s.connectionStatus << '\n';
    cout << setw(22) << "Telemetry Health" << ": " << s.telemetryHealth << '\n';
    cout << setw(22) << "Aircraft ID" << ": " << s.aircraftId << '\n';
    cout << setw(22) << "Client State" << ": " << s.clientState << '\n';
    cout << setw(22) << "Last Packet" << ": " << s.lastPacket << '\n';
    cout << setw(22) << "Weather Map" << ": " << s.weatherMapStatus << '\n';

    printDivider('-');
    cout << "Telemetry Output\n";
    printDivider('-');

    cout << fixed << setprecision(4);
    cout << setw(22) << "Latitude" << ": " << s.latitude << '\n';
    cout << setw(22) << "Longitude" << ": " << s.longitude << '\n';

    cout << setprecision(1);
    cout << setw(22) << "Altitude (ft)" << ": " << s.altitude << '\n';
    cout << setw(22) << "Speed (knots)" << ": " << s.speed << '\n';
    cout << setw(22) << "Heading (deg)" << ": " << s.heading << '\n';

    printDivider('-');
    cout << "Pilot Actions\n";
    printDivider('-');
    cout << "1. Connect to Ground Control\n";
    cout << "2. Disconnect\n";
    cout << "3. Send Handshake Request\n";
    cout << "4. Transmit Telemetry Packet\n";
    cout << "5. Request Weather Map\n";
    cout << "0. Exit\n";

    printDivider();
}

int main()
{
    WSADATA wsaData = {};
    SOCKET connectSocket = INVALID_SOCKET;
    struct sockaddr_in serverAddr = {};

    ClientDashboard ui;
    drawClientDashboard(ui);

    // socket initializing
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        ui.clientState = "STARTUP_ERROR";
        drawClientDashboard(ui);
        cout << "Winsock initialization failed!" << endl;
        return 1;
    }

    ui.clientState = "WINSOCK_READY";
    drawClientDashboard(ui);

    // socket create
    connectSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (connectSocket == INVALID_SOCKET)
    {
        ui.clientState = "SOCKET_ERROR";
        drawClientDashboard(ui);
        cout << "Socket creation failed!" << endl;
        WSACleanup();
        return 1;
    }

    // ground control address
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(kServerPort);

    if (inet_pton(AF_INET, kServerIpAddress, &serverAddr.sin_addr) <= 0)
    {
        ui.clientState = "ADDRESS_ERROR";
        drawClientDashboard(ui);
        cout << "Invalid address / Address not supported" << endl;
        closesocket(connectSocket);
        WSACleanup();
        return 1;
    }

    ui.clientState = "CONNECTING";
    drawClientDashboard(ui);
    cout << "Connecting to Ground Control at " << kServerIpAddress << ":" << kServerPort << "..." << endl;

    // actual connection establishing
    if (connect(connectSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        ui.connectionStatus = "FAILED";
        ui.clientState = "CONNECT_FAILED";
        drawClientDashboard(ui);
        cout << "Connection failed! Is the server running?" << endl;
        closesocket(connectSocket);
        WSACleanup();
        return 1;
    }

    ui.connectionStatus = "CONNECTED";
    ui.clientState = "CONNECTED";
    ui.lastPacket = "TCP_CONNECT";
    drawClientDashboard(ui);
    cout << "Successfully connected to Ground Control!" << endl;

    closesocket(connectSocket);
    WSACleanup();

    ui.connectionStatus = "DISCONNECTED";
    ui.clientState = "COMPLETE";
    ui.lastPacket = "DISCONNECT";
    drawClientDashboard(ui);
    cout << "[CLIENT] Disconnected." << endl;

    return 0;
}
