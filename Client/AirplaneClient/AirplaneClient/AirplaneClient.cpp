#include <iostream>
using namespace std;
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

int main()
{
    WSADATA wsaData;
    SOCKET connectSocket = INVALID_SOCKET;
    struct sockaddr_in serverAddr;

    // socket initialzing
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cout << "Winsock initialization failed!" << endl;
        return 1;
    }


    // socker create
    connectSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (connectSocket == INVALID_SOCKET) {
        cout << "Socket creation failed!" << endl;
        WSACleanup();
        return 1;
    }

    // ground control address
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(5000);

    if (inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr) <= 0) {
        cout << "Invalid address / Address not supported" << endl;
        closesocket(connectSocket);
        WSACleanup();
        return 1;
    }

    cout << "Connecting to Ground Control at 127.0.0.1:5000..." << endl;

    // actual connection establishing
    if (connect(connectSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cout << "Connection failed! Is the server running?" << endl;
        closesocket(connectSocket);
        WSACleanup();
        return 1;
    }

    cout << "Successfully connected to Ground Control!" << endl;

    string testMsg = "sending from client aircraft";
    send(connectSocket, testMsg.c_str(), (int)testMsg.length(), 0);

    cout << "[CLIENT] Sent message: " << testMsg << endl;

    closesocket(connectSocket);
    WSACleanup();

    cout << "[CLIENT] Disconnected." << endl;
    return 0;

}

