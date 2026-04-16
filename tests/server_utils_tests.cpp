#include <gtest/gtest.h>

#include "server_utils.h"
#include "state_machine.h"

TEST(ServerUtilsTest, ValidateAircraftIdServerSideValidId) {
    // SVR-UT-016, REQ-SVR-020: Server accepts valid AC-### aircraft IDs during handshake.
    EXPECT_TRUE(server::validateAircraftId("AC-001"));
}

TEST(ServerUtilsTest, ValidateAircraftIdServerSideInvalidId) {
    // SVR-UT-017, REQ-SVR-020: Server rejects malformed aircraft IDs during handshake.
    EXPECT_FALSE(server::validateAircraftId("AC-01"));
    EXPECT_FALSE(server::validateAircraftId("001"));
    EXPECT_FALSE(server::validateAircraftId("AC-ABC"));
}

TEST(ServerUtilsTest, VerifiedSessionStateAllowsOnlyConnectedAndTelemetry) {
    // SVR-UT-027, REQ-SVR-050: Server-originated command packets require a verified session state.
    EXPECT_FALSE(server::isVerifiedSessionState(StateMachine::State::DISCONNECTED));
    EXPECT_FALSE(server::isVerifiedSessionState(StateMachine::State::HANDSHAKE_PENDING));
    EXPECT_TRUE(server::isVerifiedSessionState(StateMachine::State::CONNECTED));
    EXPECT_TRUE(server::isVerifiedSessionState(StateMachine::State::TELEMETRY));
    EXPECT_FALSE(server::isVerifiedSessionState(StateMachine::State::LARGE_FILE_TRANSFER));
    EXPECT_FALSE(server::isVerifiedSessionState(StateMachine::State::FAULT));
}
