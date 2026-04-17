#include <gtest/gtest.h>

#include "dashboard_state.h"

TEST(DashboardStateTest, ShortStateLabelCoversAllStates) {
    EXPECT_STREQ(dashboardShortStateLabel(StateMachine::State::DISCONNECTED), "DISCONNECTED");
    EXPECT_STREQ(dashboardShortStateLabel(StateMachine::State::HANDSHAKE_PENDING), "HANDSHAKE_PENDING");
    EXPECT_STREQ(dashboardShortStateLabel(StateMachine::State::CONNECTED), "CONNECTED");
    EXPECT_STREQ(dashboardShortStateLabel(StateMachine::State::TELEMETRY), "TELEMETRY");
    EXPECT_STREQ(dashboardShortStateLabel(StateMachine::State::LARGE_FILE_TRANSFER), "LARGE_FILE_TRANSFER");
    EXPECT_STREQ(dashboardShortStateLabel(StateMachine::State::FAULT), "FAULT");
}

TEST(DashboardStateTest, SelectedAircraftFallsBackToFirstKnownAircraft) {
    DashboardState state;
    state.aircraft["AC-002"].aircraftId = "AC-002";
    state.aircraft["AC-001"].aircraftId = "AC-001";

    AircraftDashboardState* aircraft = selectedAircraft(state);
    ASSERT_NE(aircraft, nullptr);
    EXPECT_EQ(state.selectedAircraftId, "AC-001");
    EXPECT_EQ(aircraft->aircraftId, "AC-001");
}

TEST(DashboardStateTest, SelectedAircraftClearsSelectionWhenDashboardIsEmpty) {
    DashboardState state;
    state.selectedAircraftId = "AC-999";

    EXPECT_EQ(selectedAircraft(state), nullptr);
    EXPECT_TRUE(state.selectedAircraftId.empty());
}

TEST(DashboardStateTest, SelectRelativeAircraftCyclesForwardAndBackward) {
    DashboardState state;
    state.aircraft["AC-001"].aircraftId = "AC-001";
    state.aircraft["AC-002"].aircraftId = "AC-002";
    state.aircraft["AC-003"].aircraftId = "AC-003";
    state.selectedAircraftId = "AC-002";

    selectRelativeAircraft(state, 1);
    EXPECT_EQ(state.selectedAircraftId, "AC-003");

    selectRelativeAircraft(state, 1);
    EXPECT_EQ(state.selectedAircraftId, "AC-001");

    selectRelativeAircraft(state, -1);
    EXPECT_EQ(state.selectedAircraftId, "AC-003");
}

TEST(DashboardStateTest, SelectRelativeAircraftChoosesFirstWhenSelectionIsMissing) {
    DashboardState state;
    state.aircraft["AC-010"].aircraftId = "AC-010";
    state.aircraft["AC-011"].aircraftId = "AC-011";
    state.selectedAircraftId = "UNKNOWN";

    selectRelativeAircraft(state, 1);
    EXPECT_EQ(state.selectedAircraftId, "AC-010");
}
