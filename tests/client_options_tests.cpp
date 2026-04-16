#include <gtest/gtest.h>

#include "client_options.h"
#include "client_utils.h"
#include "test_helpers.h"

TEST(ClientOptionsTest, DefaultsUseLocalhostDefaultPortAndAutoId) {
    // CLT-UT-011, REQ-CLT-020: Default CLI options auto-assign a valid aircraft ID.
    const auto options = parseClientArgs({"aircraft_client"});

    ASSERT_TRUE(options.has_value());
    EXPECT_EQ(options->host, "localhost");
    EXPECT_EQ(options->port, client::kDefaultClientPort);
    EXPECT_FALSE(options->aircraftIdExplicit);
    EXPECT_TRUE(client::validateAircraftId(options->aircraftId));
}

TEST(ClientOptionsTest, ParsesCustomHostPortAndAircraftId) {
    // CLT-UT-012, REQ-CLT-020: Explicit CLI aircraft IDs are preserved after validation.
    const auto options = parseClientArgs({"aircraft_client", "127.0.0.1", "5050", "AC-123"});

    ASSERT_TRUE(options.has_value());
    EXPECT_EQ(options->host, "127.0.0.1");
    EXPECT_EQ(options->port, 5050);
    EXPECT_EQ(options->aircraftId, "AC-123");
    EXPECT_TRUE(options->aircraftIdExplicit);
}

TEST(ClientOptionsTest, RejectsOutOfRangePort) {
    // CLT-UT-013, REQ-CLT-020: Client option parsing rejects invalid TCP port values.
    EXPECT_FALSE(parseClientArgs({"aircraft_client", "localhost", "70000"}).has_value());
}

TEST(ClientOptionsTest, RejectsMalformedExplicitAircraftId) {
    // CLT-UT-014, REQ-CLT-020: Client option parsing rejects malformed explicit IDs.
    EXPECT_FALSE(parseClientArgs({"aircraft_client", "localhost", "5000", "AC-12"}).has_value());
}
