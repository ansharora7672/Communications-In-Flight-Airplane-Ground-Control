#include <iostream>
using namespace std;
#include <winsock2.h>
#include "ServerState.h"

#pragma comment(lib, "ws2_32.lib")

int main()
{
    WSADATA wsaData;
    SOCKET listenSocket = INVALID_SOCKET;
    SOCKET clientSocket = INVALID_SOCKET;
    struct sockaddr_in serverAddr;

    // current server state is disconnected
    ServerState currentState = STATE_DISCONNECTED;

   // initializing socket
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    // creating socket and binding
    listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(5000);

    bind(listenSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
    listen(listenSocket, SOMAXCONN);

    cout << "current server state is STATE_DISCONNECTED" << endl;
    cout << "waiting for aircraft on port 5000" << endl;

    // listen for 1 at a time as of now. I am doing this jsut for testing.
    // later to allow the server to accept all requets we will run this in a while true loop.
    clientSocket = accept(listenSocket, NULL, NULL);

    if (clientSocket != INVALID_SOCKET) {
        // changing the server state
        currentState = STATE_HANDSHAKE_PENDING;

        // for just testing 
        char buffer[512];
        int bytesReceived = recv(clientSocket, buffer, 512, 0);

        if (bytesReceived > 0) {
            // adding null terminator
            buffer[bytesReceived] = '\0';
            cout << "Message received from Aircraft: " << buffer << endl;

        }

    }
    // Cleanup
    closesocket(clientSocket);
    closesocket(listenSocket);
    WSACleanup();


	return 0;
}
