#pragma once

#include <deque>
#include <string>

#include "imgui.h"

#include "packet.h"
#include "state_machine.h"

struct DashboardState {
    StateMachine::State state = StateMachine::State::DISCONNECTED;
    std::string aircraftId = "N/A";
    bool connected = false;
    bool telemetryValid = false;
    TelemetryPayload telemetry {};
    std::deque<std::string> recentLogEntries;
    bool requestSendWeather = false;
    bool requestDisconnect = false;
    std::string alertMessage;
};

ImVec4 stateColor(StateMachine::State state);
void renderDashboard(DashboardState& state);
