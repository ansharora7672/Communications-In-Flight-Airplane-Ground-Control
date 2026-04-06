#pragma once
#include <cstdint>
#include "PacketType.h"

#pragma pack(push, 1)
struct PacketHeader
{
    PacketType packet_type;
    uint32_t aircraft_id;
    uint32_t sequence_number;
    uint32_t payload_size;
    uint32_t checksum;   // ✅ NEW
};
#pragma pack(pop)

static_assert(sizeof(PacketHeader) == 20, "PacketHeader must be 20 bytes");