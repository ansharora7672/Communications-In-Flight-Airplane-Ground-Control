#include <gtest/gtest.h>

#include <vector>

#include "server_session_logic.h"

TEST(ServerSessionLogicTest, AircraftIdInUseRejectsActiveDuplicateAndIgnoresCurrentSocket) {
    const std::vector<server::ActiveSessionSummary> sessions {
        {11, "AC-001", StateMachine::State::CONNECTED},
        {12, "AC-002", StateMachine::State::DISCONNECTED},
        {13, "AC-003", StateMachine::State::TELEMETRY},
    };

    EXPECT_TRUE(server::aircraftIdInUse(sessions, 99, "AC-001"));
    EXPECT_FALSE(server::aircraftIdInUse(sessions, 11, "AC-001"));
    EXPECT_FALSE(server::aircraftIdInUse(sessions, 99, "AC-002"));
}

TEST(ServerSessionLogicTest, ValidHandshakeRequestRequiresExpectedShapeAndUniqueAircraftId) {
    const PacketHeader valid = makeHeader(PacketType::HANDSHAKE_REQUEST, "AC-123", 1, 0, 0);
    const PacketHeader wrongType = makeHeader(PacketType::TELEMETRY, "AC-123", 1, 0, 0);
    const PacketHeader wrongPayload = makeHeader(PacketType::HANDSHAKE_REQUEST, "AC-123", 1, 4, 0);

    EXPECT_TRUE(server::isValidHandshakeRequest(valid, "AC-123", false));
    EXPECT_FALSE(server::isValidHandshakeRequest(valid, "BAD", false));
    EXPECT_FALSE(server::isValidHandshakeRequest(valid, "AC-123", true));
    EXPECT_FALSE(server::isValidHandshakeRequest(wrongType, "AC-123", false));
    EXPECT_FALSE(server::isValidHandshakeRequest(wrongPayload, "AC-123", false));
}

TEST(ServerSessionLogicTest, CompactPacketLogIncludesTimestampDirectionAndAircraft) {
    const PacketHeader header = makeHeader(PacketType::LARGE_FILE, "AC-555", 8, 1024, 0);
    const std::string logLine = server::compactPacketLog("17:22:00", "TX", header);

    EXPECT_EQ(logLine, "[17:22:00] TX LARGE_FILE Seq=8 AC-555");
}

TEST(ServerSessionLogicTest, IgnoredOperatorRequestMessageIncludesStateAndAction) {
    const std::string message =
        server::ignoredOperatorRequestMessage("AC-701", StateMachine::State::HANDSHAKE_PENDING, "disconnect");

    EXPECT_NE(message.find("Ignored operator disconnect request for AC-701"), std::string::npos);
    EXPECT_NE(message.find("STATE_HANDSHAKE_PENDING"), std::string::npos);
}

TEST(ServerSessionLogicTest, FaultDashboardEntryFallsBackToUnknownAircraft) {
    EXPECT_EQ(server::faultDashboardEntry("17:22:09", "", "SocketTimeout"), "[17:22:09] FAULT UNKNOWN SocketTimeout");
    EXPECT_EQ(
        server::faultDashboardEntry("17:22:09", "AC-808", "ChecksumMismatch"),
        "[17:22:09] FAULT AC-808 ChecksumMismatch");
}
