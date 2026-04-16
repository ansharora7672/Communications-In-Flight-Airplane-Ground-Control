#include <gtest/gtest.h>

#include <chrono>

#include "client_session.h"
#include "packet.h"
#include "socket_utils.h"
#include "state_machine.h"

TEST(ClientSessionTest, InitializesDisconnectedWithFirstServerSequence) {
    // SVR-UT-021, REQ-STM-010: Session objects start disconnected with sequence tracking initialized.
    server::ClientSession session;

    EXPECT_EQ(session.socket(), INVALID_SOCK);
    EXPECT_EQ(session.stateMachine().getState(), StateMachine::State::DISCONNECTED);
    EXPECT_FALSE(session.hasTelemetry());
    EXPECT_EQ(session.serverSequence(), 1u);
    EXPECT_EQ(session.lastClientSequence(), 0u);
}

TEST(ClientSessionTest, TracksTelemetryAndSequenceNumbers) {
    // SVR-UT-022, REQ-PKT-040: Session objects retain latest telemetry and packet sequence evidence.
    server::ClientSession session;
    const TelemetryPayload telemetry {43.46f, -80.52f, 35000.0f, 480.0f, 270.0f};

    EXPECT_EQ(session.consumeServerSequence(), 1u);
    EXPECT_EQ(session.serverSequence(), 2u);
    session.markPacketReceived(42);
    session.updateTelemetry(telemetry);

    EXPECT_EQ(session.lastClientSequence(), 42u);
    EXPECT_TRUE(session.hasTelemetry());
    EXPECT_NEAR(session.telemetry().latitude, 43.46f, 0.001f);
    EXPECT_NEAR(session.telemetry().longitude, -80.52f, 0.001f);
}

TEST(ClientSessionTest, TimeoutDependsOnActiveState) {
    // SVR-UT-023, REQ-STM-030: Active sessions time out according to their current state.
    server::ClientSession session;
    const auto start = std::chrono::steady_clock::now();

    session.touch(start);
    ASSERT_TRUE(session.stateMachine().transition(StateMachine::State::HANDSHAKE_PENDING));
    EXPECT_FALSE(session.timedOut(start + std::chrono::seconds(4)));
    EXPECT_TRUE(session.timedOut(start + std::chrono::seconds(5)));

    ASSERT_TRUE(session.stateMachine().transition(StateMachine::State::CONNECTED));
    ASSERT_TRUE(session.stateMachine().transition(StateMachine::State::TELEMETRY));
    session.touch(start);
    EXPECT_FALSE(session.timedOut(start + std::chrono::seconds(2)));
    EXPECT_TRUE(session.timedOut(start + std::chrono::seconds(3)));
}
