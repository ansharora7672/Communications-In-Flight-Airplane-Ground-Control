#pragma once

#include <cstdint>
#include <string>

#include "packet.h"

namespace server {

struct WeatherMapContext {
    std::string aircraftId;
    TelemetryPayload telemetry {};
    bool telemetryValid = false;
};

std::string generatedWeatherMapPathFor(const std::string& aircraftId, std::uint32_t transferSequence);
std::uint32_t aircraftSeed(const std::string& aircraftId);
void generateWeatherMap(const std::string& path, const WeatherMapContext& context, std::uint32_t transferSequence);

} // namespace server
