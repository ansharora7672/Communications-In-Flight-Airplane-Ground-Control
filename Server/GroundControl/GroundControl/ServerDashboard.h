#pragma once

#include <cstdint>
#include <string>

struct ServerDashboard
{
    std::string serverStatus = "OFFLINE";
    std::string connectionStatus = "NO CLIENT";
    std::string telemetryAlert = "STABLE";
    std::string operatorState = "DISCONNECTED";
    std::string selectedAircraft = "NONE";
    std::string lastEvent = "SERVER NOT STARTED";
    int listeningPort = 5000;

    std::string packetType = "NONE";
    uint32_t aircraftId = 0;
    uint32_t sequenceNumber = 0;
    uint32_t payloadSize = 0;

    double latitude = 0.0;
    double longitude = 0.0;
    double altitude = 0.0;
    double speed = 0.0;
    double heading = 0.0;
};