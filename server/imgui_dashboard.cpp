#include "imgui_dashboard.h"

#include <cstdio>
#include <string>

#include "server_utils.h"

namespace {

void renderTelemetryValue(const char* label, const char* value) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(120.0f);
    ImGui::TextUnformatted(value);
}

} // namespace

ImVec4 stateColor(StateMachine::State state) {
    switch (state) {
        case StateMachine::State::DISCONNECTED:
            return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
        case StateMachine::State::HANDSHAKE_PENDING:
            return ImVec4(0.9f, 0.8f, 0.2f, 1.0f);
        case StateMachine::State::CONNECTED:
        case StateMachine::State::TELEMETRY:
            return ImVec4(0.2f, 0.8f, 0.3f, 1.0f);
        case StateMachine::State::LARGE_FILE_TRANSFER:
            return ImVec4(0.2f, 0.5f, 0.9f, 1.0f);
        case StateMachine::State::FAULT:
            return ImVec4(0.9f, 0.2f, 0.2f, 1.0f);
    }
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

void renderDashboard(DashboardState& state) {
    ImGui::SetNextWindowSize(ImVec2(1080.0f, 760.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Ground Control Operator Dashboard");

    ImGui::TextUnformatted("GROUND CONTROL OPERATOR DASHBOARD");
    ImGui::Separator();

    AircraftDashboardState* selected = selectedAircraft(state);
    const bool canControlSelected = selected != nullptr && server::isVerifiedSessionState(selected->state);

    ImGui::BeginChild("aircraft_panel", ImVec2(300.0f, 260.0f), true);
    ImGui::TextUnformatted("TRACKED AIRCRAFT");
    ImGui::Separator();
    ImGui::Text("Known Aircraft: %d", static_cast<int>(state.aircraft.size()));
    ImGui::BeginDisabled(state.aircraft.empty());
    if (ImGui::Button("Previous Flight", ImVec2(140.0f, 0.0f))) {
        selectRelativeAircraft(state, -1);
        selected = selectedAircraft(state);
    }
    ImGui::SameLine();
    if (ImGui::Button("Next Flight", ImVec2(-1.0f, 0.0f))) {
        selectRelativeAircraft(state, 1);
        selected = selectedAircraft(state);
    }
    ImGui::EndDisabled();
    ImGui::Spacing();

    for (auto& [aircraftId, aircraft] : state.aircraft) {
        const bool isSelected = aircraftId == state.selectedAircraftId;
        std::string label = aircraftId + " [" + dashboardShortStateLabel(aircraft.state) + "]";
        if (ImGui::Selectable(label.c_str(), isSelected)) {
            state.selectedAircraftId = aircraftId;
            selected = &aircraft;
        }
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(!canControlSelected);
    if (ImGui::Button("Send Weather Map to Selected", ImVec2(-1.0f, 0.0f)) && selected != nullptr) {
        state.weatherRequestAircraftId = selected->aircraftId;
    }
    if (ImGui::Button("Disconnect Selected", ImVec2(-1.0f, 0.0f)) && selected != nullptr) {
        state.disconnectRequestAircraftId = selected->aircraftId;
    }
    ImGui::EndDisabled();
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("telemetry_panel", ImVec2(0.0f, 260.0f), true);
    ImGui::TextUnformatted("SELECTED AIRCRAFT");
    ImGui::Separator();

    if (selected == nullptr) {
        ImGui::TextUnformatted("No aircraft connected yet.");
    } else {
        ImGui::Text("Viewing %s", selected->aircraftId.c_str());
        renderTelemetryValue("Aircraft ID", selected->aircraftId.c_str());
        ImGui::Text("State:");
        ImGui::SameLine();
        ImGui::TextColored(stateColor(selected->state), "%s", dashboardShortStateLabel(selected->state));

        char buffer[64] {};
        if (selected->telemetryValid) {
            std::snprintf(buffer, sizeof(buffer), "%.4lf", static_cast<double>(selected->telemetry.latitude));
            renderTelemetryValue("Latitude", buffer);
            std::snprintf(buffer, sizeof(buffer), "%.4lf", static_cast<double>(selected->telemetry.longitude));
            renderTelemetryValue("Longitude", buffer);
            std::snprintf(buffer, sizeof(buffer), "%.0lf ft", static_cast<double>(selected->telemetry.altitude));
            renderTelemetryValue("Altitude", buffer);
            std::snprintf(buffer, sizeof(buffer), "%.1lf kts", static_cast<double>(selected->telemetry.speed));
            renderTelemetryValue("Speed", buffer);
            std::snprintf(buffer, sizeof(buffer), "%.0lf deg", static_cast<double>(selected->telemetry.heading));
            renderTelemetryValue("Heading", buffer);
        } else {
            renderTelemetryValue("Latitude", "--");
            renderTelemetryValue("Longitude", "--");
            renderTelemetryValue("Altitude", "--");
            renderTelemetryValue("Speed", "--");
            renderTelemetryValue("Heading", "--");
        }

        if (!selected->alertMessage.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.95f, 0.25f, 0.25f, 1.0f), "Alert: %s", selected->alertMessage.c_str());
        }
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::BeginChild("log_panel", ImVec2(0.0f, 0.0f), true);
    ImGui::TextUnformatted("COMMUNICATION LOG (last 20 entries)");
    ImGui::Separator();
    for (const auto& entry : state.recentLogEntries) {
        ImGui::TextWrapped("%s", entry.c_str());
    }
    ImGui::EndChild();

    ImGui::End();
}
