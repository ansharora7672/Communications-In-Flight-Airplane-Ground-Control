#include "server_dashboard_sync.h"

void appendDashboardLogEntry(DashboardState& dashboard, const std::string& entry) {
    dashboard.recentLogEntries.push_front(entry);
    if (dashboard.recentLogEntries.size() > 20) {
        dashboard.recentLogEntries.pop_back();
    }
}

void syncDashboardAircraft(
    DashboardState& dashboard,
    const server::ClientSession& session,
    const std::string& alertMessage) {
    if (session.aircraftId().empty()) {
        return;
    }

    auto& aircraft = dashboard.aircraft[session.aircraftId()];
    aircraft.aircraftId = session.aircraftId();
    aircraft.state = session.stateMachine().getState();
    aircraft.connected = session.stateMachine().getState() == StateMachine::State::CONNECTED ||
                         session.stateMachine().getState() == StateMachine::State::TELEMETRY;
    aircraft.telemetry = session.telemetry();
    aircraft.telemetryValid = session.hasTelemetry();
    aircraft.alertMessage = alertMessage;

    if (dashboard.selectedAircraftId.empty()) {
        dashboard.selectedAircraftId = session.aircraftId();
    }
}
