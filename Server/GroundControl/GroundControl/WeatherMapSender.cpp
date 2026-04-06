#include "WeatherMapSender.h"

#include <fstream>
#include "PacketHeader.h"
#include "Checksum.h"
#include "../../../Shared/SocketUtils.h"

bool readFileBytes(const std::string& filePath, std::vector<uint8_t>& outBytes)
{
    std::ifstream file(filePath, std::ios::binary);
    if (!file)
    {
        return false;
    }

    file.seekg(0, std::ios::end);
    const std::streamoff fileSize = file.tellg();

    if (fileSize < 0)
    {
        return false;
    }

    file.seekg(0, std::ios::beg);

    outBytes.resize(static_cast<size_t>(fileSize));

    if (fileSize == 0)
    {
        return true;
    }

    file.read(reinterpret_cast<char*>(outBytes.data()), fileSize);

    return file.good() || file.eof();
}

bool sendWeatherMapFile(
    SOCKET socketHandle,
    uint32_t aircraftId,
    uint32_t sequenceNumber,
    const std::string& filePath)
{
    std::vector<uint8_t> fileBytes;
    if (!readFileBytes(filePath, fileBytes))
    {
        return false;
    }

    PacketHeader header = {};
    header.packet_type = PacketType::LargeFile;
    header.aircraft_id = aircraftId;
    header.sequence_number = sequenceNumber;
    header.payload_size = static_cast<uint32_t>(fileBytes.size());
    header.checksum = (header.payload_size > 0)
        ? computeChecksum(fileBytes.data(), header.payload_size)
        : 0;

    if (!sendAll(socketHandle, &header, static_cast<int>(sizeof(header))))
    {
        return false;
    }

    if (header.payload_size == 0)
    {
        return true;
    }

    return sendBuffer(socketHandle, fileBytes.data(), header.payload_size);
}