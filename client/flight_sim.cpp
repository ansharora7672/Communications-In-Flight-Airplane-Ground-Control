#include "flight_sim.h"

namespace client {

void FlightSim::tick() {
    lat += static_cast<float>(drift10(rng) - 5) * 0.0001f;
    lon += static_cast<float>(drift10(rng) - 5) * 0.0001f;
    alt += static_cast<float>(drift10(rng) - 5);
    speed += static_cast<float>(drift6(rng) - 3) * 0.5f;
}

TelemetryPayload FlightSim::telemetryPayload() const {
    return TelemetryPayload {lat, lon, alt, speed, heading};
}

} // namespace client
