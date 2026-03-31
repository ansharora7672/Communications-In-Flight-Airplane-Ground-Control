#pragma once
#include <cstdint>
#include <vector>

struct LargeFilePayload
{
    std::vector<uint8_t> data; // raw file bytes
};