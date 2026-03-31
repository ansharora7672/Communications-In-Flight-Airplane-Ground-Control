#pragma once
#include <cstdint>
#include "PacketType.h"

// Stop padding from sneaking in
#pragma pack(push, 1)
struct PacketHeader
{
    PacketType packet_type;
    uint32_t aircraft_id;
    uint32_t sequence_number;
    uint32_t payload_size;
};
#pragma pack(pop)

// Make sure that PacketHeader has 4 fields by 4 bytes (16 bytes total)
static_assert(sizeof(PacketHeader) == 16, "PacketHeader must be 16 bytes");