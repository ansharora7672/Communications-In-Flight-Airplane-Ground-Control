#include <gtest/gtest.h>

#include <string>

#include "weather_map.h"

TEST(WeatherMapTest, GeneratedPathIncludesAircraftSequenceAndBmpSuffix) {
    // SVR-UT-019, REQ-SVR-060: Generated weather maps are traceable by aircraft and sequence.
    const std::string path = server::generatedWeatherMapPathFor("AC-042", 7);

    EXPECT_NE(path.find("runtime/bitmaps/generated/"), std::string::npos);
    EXPECT_NE(path.find("AC-042"), std::string::npos);
    EXPECT_NE(path.find("seq0007"), std::string::npos);
    EXPECT_NE(path.find("_weather_map.bmp"), std::string::npos);
}

TEST(WeatherMapTest, AircraftSeedIsDeterministicAndAircraftSpecific) {
    // SVR-UT-020, REQ-SVR-060: Weather-map generation uses stable aircraft-specific seeds.
    EXPECT_EQ(server::aircraftSeed("AC-001"), server::aircraftSeed("AC-001"));
    EXPECT_NE(server::aircraftSeed("AC-001"), server::aircraftSeed("AC-002"));
}
