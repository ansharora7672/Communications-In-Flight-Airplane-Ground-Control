#include <gtest/gtest.h>

#include "packet.h"

TEST(TelemetryPayloadTest, ContainsAllFields) {
    // REQ-PKT-040: Telemetry payload stores aircraft position, altitude, speed, and heading.
    TelemetryPayload payload {};
    payload.latitude = 43.46f;
    payload.longitude = -80.52f;
    payload.altitude = 35000.0f;
    payload.speed = 480.0f;
    payload.heading = 270.0f;

    EXPECT_NEAR(payload.latitude, 43.46f, 0.001f);
    EXPECT_NEAR(payload.longitude, -80.52f, 0.001f);
    EXPECT_NEAR(payload.altitude, 35000.0f, 0.001f);
    EXPECT_NEAR(payload.speed, 480.0f, 0.001f);
    EXPECT_NEAR(payload.heading, 270.0f, 0.001f);
}

TEST(TelemetryPayloadTest, Size) {
    // REQ-PKT-040: Telemetry payload contains five 4-byte float fields.
    EXPECT_EQ(sizeof(TelemetryPayload), 20u);
}
