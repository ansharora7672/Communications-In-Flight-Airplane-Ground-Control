#pragma once

#include <cstdint>
#include <ctime>
#include <fstream>
#include <string>

class PacketLogger
{
public:
    explicit PacketLogger(const std::string& filePath)
        : stream_(filePath.c_str(), std::ios::out | std::ios::trunc)
    {
        if (stream_.is_open())
        {
            stream_ << "UTC_TIMESTAMP DIRECTION PACKET_TYPE AIRCRAFT_ID SEQUENCE_NUMBER PAYLOAD_SIZE\n";
            stream_.flush();
        }
    }

    bool isOpen() const
    {
        return stream_.is_open();
    }

    void logPacket(const char* direction,
                   const char* packetType,
                   uint32_t aircraftId,
                   uint32_t sequenceNumber,
                   uint32_t payloadSize)
    {
        if (!stream_.is_open())
        {
            return;
        }

        stream_ << currentUtcTimestamp()
                << ' ' << direction
                << ' ' << packetType
                << ' ' << aircraftId
                << ' ' << sequenceNumber
                << ' ' << payloadSize
                << '\n';
        stream_.flush();
    }

private:
    static std::string currentUtcTimestamp()
    {
        const std::time_t now = std::time(NULL);
        std::tm utcTime = {};
        gmtime_s(&utcTime, &now);

        char buffer[32] = {};
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utcTime);
        return std::string(buffer);
    }

    std::ofstream stream_;
};
