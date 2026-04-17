#pragma once

#include <deque>
#include <map>
#include <string>

#include "packet.h"
#include "state_machine.h"

struct AircraftDashboardState {
    std::string aircraftId = "N/A";
    StateMachine::State state = StateMachine::State::DISCONNECTED;
    bool connected = false;
    bool telemetryValid = false;
    TelemetryPayload telemetry {};
    std::string alertMessage;
};

struct DashboardState {
    std::map<std::string, AircraftDashboardState> aircraft;
    std::string selectedAircraftId;
    std::deque<std::string> recentLogEntries;
    std::string weatherRequestAircraftId;
    std::string disconnectRequestAircraftId;
};

const char* dashboardShortStateLabel(StateMachine::State state);
AircraftDashboardState* selectedAircraft(DashboardState& state);
void selectRelativeAircraft(DashboardState& state, int direction);
