#include <gtest/gtest.h>

#include <chrono>

#include "server_dashboard_sync.h"

TEST(ServerDashboardSyncTest, AppendDashboardLogEntryKeepsNewestTwentyEntries) {
    DashboardState state;
    for (int i = 0; i < 25; ++i) {
        appendDashboardLogEntry(state, "entry-" + std::to_string(i));
    }

    ASSERT_EQ(state.recentLogEntries.size(), 20u);
    EXPECT_EQ(state.recentLogEntries.front(), "entry-24");
    EXPECT_EQ(state.recentLogEntries.back(), "entry-5");
}

TEST(ServerDashboardSyncTest, SyncDashboardAircraftPopulatesSelectionAndTelemetry) {
    server::ClientSession session;
    ASSERT_TRUE(session.stateMachine().transition(StateMachine::State::HANDSHAKE_PENDING));
    ASSERT_TRUE(session.stateMachine().transition(StateMachine::State::CONNECTED));
    ASSERT_TRUE(session.stateMachine().transition(StateMachine::State::TELEMETRY));
    session.setAircraftId("AC-201");
    session.updateTelemetry(TelemetryPayload {45.0f, -80.0f, 33000.0f, 470.0f, 180.0f});

    DashboardState state;
    syncDashboardAircraft(state, session, "Telemetry rate degraded");

    const auto aircraftIt = state.aircraft.find("AC-201");
    ASSERT_NE(aircraftIt, state.aircraft.end());
    const AircraftDashboardState& aircraft = aircraftIt->second;
    EXPECT_EQ(state.selectedAircraftId, "AC-201");
    EXPECT_EQ(aircraft.aircraftId, "AC-201");
    EXPECT_EQ(aircraft.state, StateMachine::State::TELEMETRY);
    EXPECT_TRUE(aircraft.connected);
    EXPECT_TRUE(aircraft.telemetryValid);
    EXPECT_EQ(aircraft.alertMessage, "Telemetry rate degraded");
    EXPECT_NEAR(aircraft.telemetry.latitude, 45.0f, 0.001f);
}

TEST(ServerDashboardSyncTest, SyncDashboardAircraftIgnoresSessionsWithoutAircraftId) {
    server::ClientSession session;
    DashboardState state;
    state.selectedAircraftId = "AC-999";

    syncDashboardAircraft(state, session);

    EXPECT_TRUE(state.aircraft.empty());
    EXPECT_EQ(state.selectedAircraftId, "AC-999");
}
