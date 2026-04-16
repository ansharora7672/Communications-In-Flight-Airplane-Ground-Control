#include <gtest/gtest.h>

#include <cstdint>
#include <set>
#include <string>

#include "client_utils.h"

TEST(ClientUtilsTest, ValidateAircraftIdValidFormat) {
    // CLT-UT-001, REQ-CLT-020: Client accepts valid AC-### aircraft IDs.
    EXPECT_TRUE(client::validateAircraftId("AC-001"));
    EXPECT_TRUE(client::validateAircraftId("AC-042"));
    EXPECT_TRUE(client::validateAircraftId("AC-999"));
}

TEST(ClientUtilsTest, ValidateAircraftIdInvalidFormatNoPrefix) {
    // CLT-UT-002, REQ-CLT-020: Client rejects IDs without the AC- prefix.
    EXPECT_FALSE(client::validateAircraftId("001"));
}

TEST(ClientUtilsTest, ValidateAircraftIdInvalidFormatTooShort) {
    // CLT-UT-003, REQ-CLT-020: Client rejects two-digit aircraft IDs.
    EXPECT_FALSE(client::validateAircraftId("AC-01"));
}

TEST(ClientUtilsTest, ValidateAircraftIdInvalidFormatLetters) {
    // CLT-UT-004, REQ-CLT-020: Client rejects non-numeric aircraft ID suffixes.
    EXPECT_FALSE(client::validateAircraftId("AC-ABC"));
}

TEST(ClientUtilsTest, ValidateAircraftIdEmptyString) {
    // CLT-UT-005, REQ-CLT-020: Client rejects an empty aircraft ID.
    EXPECT_FALSE(client::validateAircraftId(""));
}

TEST(ClientUtilsTest, GenerateAutoAircraftIdFormatIsValid) {
    // CLT-UT-006, REQ-CLT-020: Auto-generated aircraft IDs match AC-###.
    EXPECT_TRUE(client::validateAircraftId(client::generateAutoAircraftId()));
}

TEST(ClientUtilsTest, GenerateAutoAircraftIdRetrySaltsProduceValidCandidatePool) {
    // CLT-UT-007, REQ-CLT-020: Retry salts generate valid candidate IDs for handshake retry.
    std::set<std::string> generatedIds;
    for (std::uint32_t salt = 0; salt < 16; ++salt) {
        const std::string aircraftId = client::generateAutoAircraftId(salt);
        EXPECT_TRUE(client::validateAircraftId(aircraftId));
        generatedIds.insert(aircraftId);
    }

    EXPECT_GE(generatedIds.size(), 2u);
}

TEST(ClientUtilsTest, ClientStateTelemetryStateStringRepresentation) {
    // CLT-UT-008, REQ-CLT-050: TELEMETRY state has the expected CLI string.
    EXPECT_EQ(client::stateToString(client::ClientState::TELEMETRY), "TELEMETRY");
}
