// PacketType.h
#pragma once
#include <cstdint>

enum class PacketType : uint32_t
{
    HandshakeRequest = 1,
    HandshakeAck = 2,
    Telemetry = 3,
    LargeFile = 4,
    Command = 5
};