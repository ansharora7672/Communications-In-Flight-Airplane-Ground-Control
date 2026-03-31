#pragma once

#include <winsock2.h>

#include <climits>
#include <cstdint>
#include <memory>
#include <new>

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

inline bool sendBuffer(SOCKET socketHandle, const void* buffer, uint32_t totalBytes)
{
    if (totalBytes == 0)
    {
        return true;
    }

    if (totalBytes > static_cast<uint32_t>(INT_MAX))
    {
        return false;
    }

    return sendAll(socketHandle, buffer, static_cast<int>(totalBytes));
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

inline bool receiveDynamicBuffer(SOCKET socketHandle, uint32_t payloadSize, std::unique_ptr<uint8_t[]>& payloadBuffer)
{
    payloadBuffer.reset();

    if (payloadSize == 0)
    {
        return true;
    }

    if (payloadSize > static_cast<uint32_t>(INT_MAX))
    {
        return false;
    }

    std::unique_ptr<uint8_t[]> buffer(new (std::nothrow) uint8_t[payloadSize]);
    if (!buffer)
    {
        return false;
    }

    if (!recvAll(socketHandle, buffer.get(), static_cast<int>(payloadSize)))
    {
        return false;
    }

    payloadBuffer = std::move(buffer);
    return true;
}
