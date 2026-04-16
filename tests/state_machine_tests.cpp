#include <gtest/gtest.h>

#include "state_machine.h"

TEST(StateMachineTest, InitialState) {
    // REQ-STM-010: State machine initializes disconnected.
    StateMachine machine;

    EXPECT_EQ(machine.getState(), StateMachine::State::DISCONNECTED);
}

TEST(StateMachineTest, ValidTransitionDisconnectedToHandshakePending) {
    // REQ-STM-010: DISCONNECTED can transition to HANDSHAKE_PENDING.
    StateMachine machine;

    EXPECT_TRUE(machine.transition(StateMachine::State::HANDSHAKE_PENDING));
    EXPECT_EQ(machine.getState(), StateMachine::State::HANDSHAKE_PENDING);
}

TEST(StateMachineTest, ValidTransitionHandshakePendingToConnected) {
    // REQ-STM-020: Handshake must complete before CONNECTED.
    StateMachine machine;

    EXPECT_TRUE(machine.transition(StateMachine::State::HANDSHAKE_PENDING));
    EXPECT_TRUE(machine.transition(StateMachine::State::CONNECTED));
    EXPECT_EQ(machine.getState(), StateMachine::State::CONNECTED);
}

TEST(StateMachineTest, ValidTransitionConnectedToTelemetry) {
    // REQ-STM-020: TELEMETRY is allowed only after CONNECTED.
    StateMachine machine;

    EXPECT_TRUE(machine.transition(StateMachine::State::HANDSHAKE_PENDING));
    EXPECT_TRUE(machine.transition(StateMachine::State::CONNECTED));
    EXPECT_TRUE(machine.transition(StateMachine::State::TELEMETRY));
    EXPECT_EQ(machine.getState(), StateMachine::State::TELEMETRY);
}

TEST(StateMachineTest, ValidTransitionTelemetryToConnected) {
    // REQ-STM-020: TELEMETRY can return to CONNECTED after a telemetry cycle.
    StateMachine machine;

    EXPECT_TRUE(machine.transition(StateMachine::State::HANDSHAKE_PENDING));
    EXPECT_TRUE(machine.transition(StateMachine::State::CONNECTED));
    EXPECT_TRUE(machine.transition(StateMachine::State::TELEMETRY));
    EXPECT_TRUE(machine.transition(StateMachine::State::CONNECTED));
    EXPECT_EQ(machine.getState(), StateMachine::State::CONNECTED);
}

TEST(StateMachineTest, ValidTransitionConnectedToLargeFileTransfer) {
    // REQ-STM-020: LARGE_FILE_TRANSFER is allowed only after CONNECTED.
    StateMachine machine;

    EXPECT_TRUE(machine.transition(StateMachine::State::HANDSHAKE_PENDING));
    EXPECT_TRUE(machine.transition(StateMachine::State::CONNECTED));
    EXPECT_TRUE(machine.transition(StateMachine::State::LARGE_FILE_TRANSFER));
    EXPECT_EQ(machine.getState(), StateMachine::State::LARGE_FILE_TRANSFER);
}

TEST(StateMachineTest, ValidTransitionLargeFileTransferToConnected) {
    // REQ-STM-020: LARGE_FILE_TRANSFER can return to CONNECTED after transfer completion.
    StateMachine machine;

    EXPECT_TRUE(machine.transition(StateMachine::State::HANDSHAKE_PENDING));
    EXPECT_TRUE(machine.transition(StateMachine::State::CONNECTED));
    EXPECT_TRUE(machine.transition(StateMachine::State::LARGE_FILE_TRANSFER));
    EXPECT_TRUE(machine.transition(StateMachine::State::CONNECTED));
    EXPECT_EQ(machine.getState(), StateMachine::State::CONNECTED);
}

TEST(StateMachineTest, ValidTransitionAnyActiveStateToFault) {
    // REQ-STM-030: Active states can transition immediately to FAULT.
    StateMachine handshakeMachine;
    EXPECT_TRUE(handshakeMachine.transition(StateMachine::State::HANDSHAKE_PENDING));
    EXPECT_TRUE(handshakeMachine.transition(StateMachine::State::FAULT));
    EXPECT_EQ(handshakeMachine.getState(), StateMachine::State::FAULT);

    StateMachine connectedMachine;
    EXPECT_TRUE(connectedMachine.transition(StateMachine::State::HANDSHAKE_PENDING));
    EXPECT_TRUE(connectedMachine.transition(StateMachine::State::CONNECTED));
    EXPECT_TRUE(connectedMachine.transition(StateMachine::State::FAULT));
    EXPECT_EQ(connectedMachine.getState(), StateMachine::State::FAULT);

    StateMachine telemetryMachine;
    EXPECT_TRUE(telemetryMachine.transition(StateMachine::State::HANDSHAKE_PENDING));
    EXPECT_TRUE(telemetryMachine.transition(StateMachine::State::CONNECTED));
    EXPECT_TRUE(telemetryMachine.transition(StateMachine::State::TELEMETRY));
    EXPECT_TRUE(telemetryMachine.transition(StateMachine::State::FAULT));
    EXPECT_EQ(telemetryMachine.getState(), StateMachine::State::FAULT);

    StateMachine transferMachine;
    EXPECT_TRUE(transferMachine.transition(StateMachine::State::HANDSHAKE_PENDING));
    EXPECT_TRUE(transferMachine.transition(StateMachine::State::CONNECTED));
    EXPECT_TRUE(transferMachine.transition(StateMachine::State::LARGE_FILE_TRANSFER));
    EXPECT_TRUE(transferMachine.transition(StateMachine::State::FAULT));
    EXPECT_EQ(transferMachine.getState(), StateMachine::State::FAULT);
}

TEST(StateMachineTest, ValidTransitionFaultToDisconnected) {
    // REQ-STM-040: FAULT can reset to DISCONNECTED after logging.
    StateMachine machine;

    EXPECT_TRUE(machine.transition(StateMachine::State::HANDSHAKE_PENDING));
    EXPECT_TRUE(machine.transition(StateMachine::State::CONNECTED));
    EXPECT_TRUE(machine.transition(StateMachine::State::FAULT));
    EXPECT_TRUE(machine.transition(StateMachine::State::DISCONNECTED));
    EXPECT_EQ(machine.getState(), StateMachine::State::DISCONNECTED);
}

TEST(StateMachineTest, InvalidTransitionDisconnectedToConnected) {
    // REQ-STM-020: DISCONNECTED cannot skip handshake and enter CONNECTED.
    StateMachine machine;

    EXPECT_FALSE(machine.transition(StateMachine::State::CONNECTED));
    EXPECT_EQ(machine.getState(), StateMachine::State::DISCONNECTED);
}

TEST(StateMachineTest, InvalidTransitionDisconnectedToTelemetry) {
    // REQ-STM-020: DISCONNECTED cannot transmit telemetry.
    StateMachine machine;

    EXPECT_FALSE(machine.transition(StateMachine::State::TELEMETRY));
    EXPECT_EQ(machine.getState(), StateMachine::State::DISCONNECTED);
}

TEST(StateMachineTest, InvalidTransitionTelemetryToDisconnected) {
    // REQ-STM-020: TELEMETRY cannot jump directly to DISCONNECTED.
    StateMachine machine;

    EXPECT_TRUE(machine.transition(StateMachine::State::HANDSHAKE_PENDING));
    EXPECT_TRUE(machine.transition(StateMachine::State::CONNECTED));
    EXPECT_TRUE(machine.transition(StateMachine::State::TELEMETRY));
    EXPECT_FALSE(machine.transition(StateMachine::State::DISCONNECTED));
    EXPECT_EQ(machine.getState(), StateMachine::State::TELEMETRY);
}

TEST(StateMachineTest, InvalidTransitionFaultToConnected) {
    // REQ-STM-020: FAULT must reset to DISCONNECTED before reconnecting.
    StateMachine machine;

    EXPECT_TRUE(machine.transition(StateMachine::State::HANDSHAKE_PENDING));
    EXPECT_TRUE(machine.transition(StateMachine::State::CONNECTED));
    EXPECT_TRUE(machine.transition(StateMachine::State::FAULT));
    EXPECT_FALSE(machine.transition(StateMachine::State::CONNECTED));
    EXPECT_EQ(machine.getState(), StateMachine::State::FAULT);
}

TEST(StateMachineTest, SameStateTransitionIsAllowed) {
    // REQ-STM-020: Same-state transitions are allowed by the transition guard.
    StateMachine machine;

    EXPECT_TRUE(machine.transition(StateMachine::State::DISCONNECTED));
    EXPECT_EQ(machine.getState(), StateMachine::State::DISCONNECTED);
}

TEST(StateMachineTest, StateToStringAllStates) {
    // REQ-STM-010: All defined states have traceable string labels.
    StateMachine machine;

    EXPECT_EQ(machine.stateToString(StateMachine::State::DISCONNECTED), "STATE_DISCONNECTED");
    EXPECT_EQ(machine.stateToString(StateMachine::State::HANDSHAKE_PENDING), "STATE_HANDSHAKE_PENDING");
    EXPECT_EQ(machine.stateToString(StateMachine::State::CONNECTED), "STATE_CONNECTED");
    EXPECT_EQ(machine.stateToString(StateMachine::State::TELEMETRY), "STATE_TELEMETRY");
    EXPECT_EQ(machine.stateToString(StateMachine::State::LARGE_FILE_TRANSFER), "STATE_LARGE_FILE_TRANSFER");
    EXPECT_EQ(machine.stateToString(StateMachine::State::FAULT), "STATE_FAULT");
}
