#include "dashboard_state.h"

namespace {

template <typename DashboardStateType>
auto selectedAircraftImpl(DashboardStateType& state) -> decltype(&state.aircraft.begin()->second) {
    if (state.selectedAircraftId.empty() && !state.aircraft.empty()) {
        state.selectedAircraftId = state.aircraft.begin()->first;
    }

    auto selected = state.aircraft.find(state.selectedAircraftId);
    if (selected != state.aircraft.end()) {
        return &selected->second;
    }

    if (!state.aircraft.empty()) {
        state.selectedAircraftId = state.aircraft.begin()->first;
        return &state.aircraft.begin()->second;
    }

    state.selectedAircraftId.clear();
    return nullptr;
}

} // namespace

const char* dashboardShortStateLabel(StateMachine::State state) {
    switch (state) {
        case StateMachine::State::DISCONNECTED:
            return "DISCONNECTED";
        case StateMachine::State::HANDSHAKE_PENDING:
            return "HANDSHAKE_PENDING";
        case StateMachine::State::CONNECTED:
            return "CONNECTED";
        case StateMachine::State::TELEMETRY:
            return "TELEMETRY";
        case StateMachine::State::LARGE_FILE_TRANSFER:
            return "LARGE_FILE_TRANSFER";
        case StateMachine::State::FAULT:
            return "FAULT";
    }
    return "UNKNOWN";
}

AircraftDashboardState* selectedAircraft(DashboardState& state) {
    return selectedAircraftImpl(state);
}

void selectRelativeAircraft(DashboardState& state, int direction) {
    if (state.aircraft.empty()) {
        state.selectedAircraftId.clear();
        return;
    }

    auto selected = state.aircraft.find(state.selectedAircraftId);
    if (selected == state.aircraft.end()) {
        state.selectedAircraftId = state.aircraft.begin()->first;
        return;
    }

    if (direction > 0) {
        ++selected;
        if (selected == state.aircraft.end()) {
            selected = state.aircraft.begin();
        }
    } else {
        if (selected == state.aircraft.begin()) {
            selected = state.aircraft.end();
        }
        --selected;
    }

    state.selectedAircraftId = selected->first;
}
