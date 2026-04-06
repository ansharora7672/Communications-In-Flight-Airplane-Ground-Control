#pragma once

#include <chrono>
#include <string>
#include "ServerDashboard.h"
#include "PacketHeader.h"
#include "TelemetryPayload.h"

struct TelemetryMonitor
{
    bool hasTelemetry = false;
    std::chrono::steady_clock::time_point lastTelemetryTime =
        std::chrono::steady_clock::now();
};

inline const char* dashboardPacketTypeToString(PacketType packetType)
{
    switch (packetType)
    {
    case PacketType::HandshakeRequest:
        return "HANDSHAKE_REQUEST";
    case PacketType::HandshakeAck:
        return "HANDSHAKE_ACK";
    case PacketType::Telemetry:
        return "TELEMETRY";
    case PacketType::LargeFile:
        return "LARGE_FILE";
    case PacketType::Command:
        return "COMMAND";
    default:
        return "UNKNOWN";
    }
}

inline void applyHeaderToDashboard(ServerDashboard& ui, const PacketHeader& header)
{
    ui.packetType = dashboardPacketTypeToString(header.packet_type);
    ui.aircraftId = header.aircraft_id;
    ui.sequenceNumber = header.sequence_number;
    ui.payloadSize = header.payload_size;
}

inline void applyTelemetryToDashboard(ServerDashboard& ui, const TelemetryPayload& telemetry)
{
    ui.latitude = telemetry.latitude;
    ui.longitude = telemetry.longitude;
    ui.altitude = telemetry.altitude;
    ui.speed = telemetry.speed;
    ui.heading = telemetry.heading;
}

inline void markTelemetryReceived(ServerDashboard& ui, TelemetryMonitor& monitor)
{
    monitor.hasTelemetry = true;
    monitor.lastTelemetryTime = std::chrono::steady_clock::now();
    ui.telemetryAlert = "STABLE";
}

inline void refreshTelemetryAlert(ServerDashboard& ui, const TelemetryMonitor& monitor)
{
    if (!monitor.hasTelemetry)
    {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - monitor.lastTelemetryTime).count();

    if (elapsedMs > 3000)
    {
        ui.telemetryAlert = "DROP DETECTED";
    }
    else
    {
        ui.telemetryAlert = "STABLE";
    }
}

inline void simulateTelemetryUpdate(ServerDashboard& ui, TelemetryMonitor& telemetryMonitor)
{
    PacketHeader telemetryHeader = {};
    telemetryHeader.packet_type = PacketType::Telemetry;
    telemetryHeader.aircraft_id = (ui.aircraftId == 0) ? 101 : ui.aircraftId;
    telemetryHeader.sequence_number = ui.sequenceNumber + 1;
    telemetryHeader.payload_size = sizeof(TelemetryPayload);

    TelemetryPayload telemetry = {};
    telemetry.latitude = 43.6655;
    telemetry.longitude = -79.4012;
    telemetry.altitude = 32150.0;
    telemetry.speed = 447.0;
    telemetry.heading = 80.0;

    applyHeaderToDashboard(ui, telemetryHeader);
    applyTelemetryToDashboard(ui, telemetry);
    markTelemetryReceived(ui, telemetryMonitor);

    ui.connectionStatus = "CLIENT CONNECTED";
    ui.operatorState = "TELEMETRY";
    ui.selectedAircraft = "AC-" + std::to_string(telemetryHeader.aircraft_id);
    ui.lastEvent = "Telemetry packet processed";
}