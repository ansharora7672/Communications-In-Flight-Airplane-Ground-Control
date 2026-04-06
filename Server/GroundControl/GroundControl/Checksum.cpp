#include "Checksum.h"

uint32_t computeChecksum(const void* data, uint32_t size)
{
    const uint8_t* bytes = static_cast<const uint8_t*>(data);

    uint32_t sum = 0;

    for (uint32_t i = 0; i < size; ++i)
    {
        sum += bytes[i];
    }

    return sum;
}