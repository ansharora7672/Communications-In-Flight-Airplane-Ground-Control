#include <gtest/gtest.h>

#include <chrono>

#include "client_session.h"
#include "packet.h"
#include "socket_utils.h"
#include "state_machine.h"
#include "test_helpers.h"

TEST(ClientSessionTest, InitializesDisconnectedWithFirstServerSequence) {
    // SVR-UT-024, REQ-STM-010: Session objects start disconnected with sequence tracking initialized.
    server::ClientSession session;

    EXPECT_EQ(session.socket(), INVALID_SOCK);
    EXPECT_EQ(session.stateMachine().getState(), StateMachine::State::DISCONNECTED);
    EXPECT_FALSE(session.hasTelemetry());
    EXPECT_EQ(session.serverSequence(), 1u);
    EXPECT_EQ(session.lastClientSequence(), 0u);
}

TEST(ClientSessionTest, SetSocketAndCloseResetSocketHandle) {
    SocketRuntimeGuard sockets;
    ConnectedSocketPair pair = makeConnectedSocketPair();
    server::ClientSession session;

    session.setSocket(pair.server);
    pair.server = INVALID_SOCK;
    EXPECT_NE(session.socket(), INVALID_SOCK);

    session.close();
    EXPECT_EQ(session.socket(), INVALID_SOCK);
}

TEST(ClientSessionTest, TracksTelemetryAndSequenceNumbers) {
    // SVR-UT-025, REQ-PKT-040, REQ-STM-010: Session objects retain latest telemetry and packet sequence evidence.
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

TEST(ClientSessionTest, DisconnectedAndFaultStatesDoNotTimeout) {
    server::ClientSession session;
    const auto start = std::chrono::steady_clock::now();

    session.touch(start);
    EXPECT_FALSE(session.timedOut(start + std::chrono::seconds(30)));

    ASSERT_TRUE(session.stateMachine().transition(StateMachine::State::HANDSHAKE_PENDING));
    ASSERT_TRUE(session.stateMachine().transition(StateMachine::State::FAULT));
    EXPECT_FALSE(session.timedOut(start + std::chrono::seconds(30)));
}

TEST(ClientSessionTest, TimeoutDependsOnActiveState) {
    // SVR-UT-026, REQ-STM-030: Active sessions time out according to their current state.
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

TEST(ClientSessionTest, TelemetryAlertAppearsBeforeTimeout) {
    server::ClientSession session;
    const auto start = std::chrono::steady_clock::now();
    const TelemetryPayload telemetry {43.46f, -80.52f, 35000.0f, 480.0f, 270.0f};

    ASSERT_TRUE(session.stateMachine().transition(StateMachine::State::HANDSHAKE_PENDING));
    ASSERT_TRUE(session.stateMachine().transition(StateMachine::State::CONNECTED));
    ASSERT_TRUE(session.stateMachine().transition(StateMachine::State::TELEMETRY));
    session.updateTelemetry(telemetry, start);
    session.updateTelemetry(telemetry, start + std::chrono::seconds(1));

    EXPECT_FALSE(session.telemetryRateDegraded(start + std::chrono::milliseconds(2500)));
    EXPECT_TRUE(session.telemetryRateDegraded(start + std::chrono::milliseconds(3100)));
    EXPECT_NE(session.telemetryAlertMessage(start + std::chrono::milliseconds(3100)).find("Telemetry rate degraded"), std::string::npos);
    EXPECT_FALSE(session.timedOut(start + std::chrono::milliseconds(3100)));
}
