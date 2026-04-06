#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <winsock2.h>

bool readFileBytes(const std::string& filePath, std::vector<uint8_t>& outBytes);

bool sendWeatherMapFile(
    SOCKET socketHandle,
    uint32_t aircraftId,
    uint32_t sequenceNumber,
    const std::string& filePath);