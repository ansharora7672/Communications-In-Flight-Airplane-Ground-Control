#pragma once

#include <winsock2.h>

inline bool sendAll(SOCKET socketHandle, const void* buffer, int totalBytes)
{
    const char* current = static_cast<const char*>(buffer);
    int sentBytes = 0;

    while (sentBytes < totalBytes)
    {
        const int bytesSent = send(socketHandle, current + sentBytes, totalBytes - sentBytes, 0);
        if (bytesSent == SOCKET_ERROR || bytesSent == 0)
        {
            return false;
        }

        sentBytes += bytesSent;
    }

    return true;
}

inline bool recvAll(SOCKET socketHandle, void* buffer, int totalBytes)
{
    char* current = static_cast<char*>(buffer);
    int receivedBytes = 0;

    while (receivedBytes < totalBytes)
    {
        const int bytesReceived = recv(socketHandle, current + receivedBytes, totalBytes - receivedBytes, 0);
        if (bytesReceived == SOCKET_ERROR || bytesReceived == 0)
        {
            return false;
        }

        receivedBytes += bytesReceived;
    }

    return true;
}
