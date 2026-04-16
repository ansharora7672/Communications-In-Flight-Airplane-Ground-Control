#include <gtest/gtest.h>

#include "server_utils.h"

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
