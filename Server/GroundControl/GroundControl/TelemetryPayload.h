#pragma once
#include <cstdint>

#pragma pack(push, 1)
struct TelemetryPayload
{
    double latitude;
    double longitude;
    double altitude;
    double speed;
    double heading;
};
#pragma pack(pop)

static_assert(sizeof(TelemetryPayload) == 40, "TelemetryPayload must be 40 bytes");