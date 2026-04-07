#include "imgui_dashboard.h"

#include <cstdio>

namespace {

const char* shortStateLabel(StateMachine::State state) {
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
    ImGui::SetNextWindowSize(ImVec2(980.0f, 700.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Ground Control Operator Dashboard");

    ImGui::TextUnformatted("GROUND CONTROL OPERATOR DASHBOARD");
    ImGui::Separator();

    ImGui::BeginChild("connection_panel", ImVec2(270.0f, 240.0f), true);
    ImGui::TextUnformatted("CONNECTION");
    ImGui::Separator();
    ImGui::Text("Status:");
    ImGui::SameLine();
    ImGui::TextColored(stateColor(state.state), "%s", state.connected ? "CONNECTED" : "DISCONNECTED");
    ImGui::Text("State:");
    ImGui::SameLine();
    ImGui::TextColored(stateColor(state.state), "%s", shortStateLabel(state.state));
    ImGui::Text("Aircraft:");
    ImGui::SameLine();
    ImGui::TextUnformatted(state.aircraftId.c_str());
    ImGui::Spacing();
    if (ImGui::Button("Send Weather Map to Aircraft", ImVec2(-1.0f, 0.0f))) {
        state.requestSendWeather = true;
    }
    if (ImGui::Button("Disconnect", ImVec2(-1.0f, 0.0f))) {
        state.requestDisconnect = true;
    }
    if (!state.alertMessage.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.95f, 0.25f, 0.25f, 1.0f), "Alert: %s", state.alertMessage.c_str());
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("telemetry_panel", ImVec2(0.0f, 240.0f), true);
    ImGui::TextUnformatted("LIVE TELEMETRY");
    ImGui::Separator();
    renderTelemetryValue("Aircraft ID", state.aircraftId.c_str());

    char buffer[64] {};
    if (state.telemetryValid) {
        std::snprintf(buffer, sizeof(buffer), "%.4f", state.telemetry.latitude);
        renderTelemetryValue("Latitude", buffer);
        std::snprintf(buffer, sizeof(buffer), "%.4f", state.telemetry.longitude);
        renderTelemetryValue("Longitude", buffer);
        std::snprintf(buffer, sizeof(buffer), "%.0f ft", state.telemetry.altitude);
        renderTelemetryValue("Altitude", buffer);
        std::snprintf(buffer, sizeof(buffer), "%.1f kts", state.telemetry.speed);
        renderTelemetryValue("Speed", buffer);
        std::snprintf(buffer, sizeof(buffer), "%.0f deg", state.telemetry.heading);
        renderTelemetryValue("Heading", buffer);
    } else {
        renderTelemetryValue("Latitude", "--");
        renderTelemetryValue("Longitude", "--");
        renderTelemetryValue("Altitude", "--");
        renderTelemetryValue("Speed", "--");
        renderTelemetryValue("Heading", "--");
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
