#pragma once

#include <random>

#include "packet.h"

namespace client {

class FlightSim {
public:
    void tick();
    TelemetryPayload telemetryPayload() const;

private:
    float lat = 43.4643f;
    float lon = -80.5204f;
    float alt = 35000.0f;
    float speed = 480.0f;
    float heading = 270.0f;

    std::mt19937 rng {std::random_device{}()};
    std::uniform_int_distribution<int> drift10 {0, 9};
    std::uniform_int_distribution<int> drift6 {0, 5};
};

} // namespace client
