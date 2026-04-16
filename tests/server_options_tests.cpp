#include <gtest/gtest.h>

#include "server_options.h"
#include "test_helpers.h"

TEST(ServerOptionsTest, DefaultsUseDefaultListenPortAndDashboardMode) {
    // SVR-UT-018, REQ-SVR-020: Server option parsing defaults to dashboard mode on port 5000.
    const auto options = parseServerArgs({"ground_server"});

    ASSERT_TRUE(options.has_value());
    EXPECT_EQ(options->listenPort, server::kDefaultServerPort);
    EXPECT_FALSE(options->headless);
}

TEST(ServerOptionsTest, ParsesHeadlessAndCustomPort) {
    // SVR-UT-019, REQ-SVR-020: Server option parsing accepts a custom port and headless flag.
    const auto options = parseServerArgs({"ground_server", "--headless", "5050"});

    ASSERT_TRUE(options.has_value());
    EXPECT_EQ(options->listenPort, 5050);
    EXPECT_TRUE(options->headless);
}

TEST(ServerOptionsTest, RejectsOutOfRangePort) {
    // SVR-UT-020, REQ-SVR-020: Server option parsing rejects invalid TCP port values.
    EXPECT_FALSE(parseServerArgs({"ground_server", "0"}).has_value());
}

TEST(ServerOptionsTest, RejectsUnknownArgument) {
    // SVR-UT-021, REQ-SVR-020: Server option parsing rejects unsupported arguments.
    EXPECT_FALSE(parseServerArgs({"ground_server", "--bogus"}).has_value());
}
