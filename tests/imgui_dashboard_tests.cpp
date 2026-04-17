#include <gtest/gtest.h>

#include "imgui.h"
#include "imgui_dashboard.h"

namespace {

class ImGuiContextGuard {
public:
    ImGuiContextGuard() { ImGui::CreateContext(); }
    ~ImGuiContextGuard() { ImGui::DestroyContext(); }
};

} // namespace

TEST(ImGuiDashboardTest, StateColorAssignsDistinctOperationalColors) {
    const ImVec4 disconnected = stateColor(StateMachine::State::DISCONNECTED);
    const ImVec4 connected = stateColor(StateMachine::State::CONNECTED);
    const ImVec4 fault = stateColor(StateMachine::State::FAULT);

    EXPECT_FLOAT_EQ(disconnected.x, 0.6f);
    EXPECT_FLOAT_EQ(connected.y, 0.8f);
    EXPECT_FLOAT_EQ(fault.x, 0.9f);
}

TEST(ImGuiDashboardTest, RenderDashboardHandlesEmptyAndPopulatedStates) {
    ImGuiContextGuard guard;
    ImGui::GetIO().DisplaySize = ImVec2(1280.0f, 720.0f);
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ImGui::NewFrame();

    DashboardState emptyState;
    renderDashboard(emptyState);

    DashboardState populatedState;
    auto& aircraft = populatedState.aircraft["AC-901"];
    aircraft.aircraftId = "AC-901";
    aircraft.state = StateMachine::State::TELEMETRY;
    aircraft.connected = true;
    aircraft.telemetryValid = true;
    aircraft.telemetry = TelemetryPayload {44.2f, -79.9f, 32000.0f, 460.0f, 210.0f};
    aircraft.alertMessage = "Telemetry rate degraded";
    populatedState.selectedAircraftId = "AC-901";

    renderDashboard(populatedState);
    ImGui::Render();

    EXPECT_GT(ImGui::GetDrawData()->CmdListsCount, 0);
}
