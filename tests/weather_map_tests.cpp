#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "test_helpers.h"
#include "weather_map.h"

TEST(WeatherMapTest, GeneratedPathIncludesAircraftSequenceAndBmpSuffix) {
    // SVR-UT-022, REQ-SVR-060: Generated weather maps are traceable by aircraft and sequence.
    const std::string path = server::generatedWeatherMapPathFor("AC-042", 7);

    EXPECT_NE(path.find("runtime/bitmaps/generated/"), std::string::npos);
    EXPECT_NE(path.find("AC-042"), std::string::npos);
    EXPECT_NE(path.find("seq0007"), std::string::npos);
    EXPECT_NE(path.find("_weather_map.bmp"), std::string::npos);
}

TEST(WeatherMapTest, AircraftSeedIsDeterministicAndAircraftSpecific) {
    // SVR-UT-023, REQ-SVR-060: Weather-map generation uses stable aircraft-specific seeds.
    EXPECT_EQ(server::aircraftSeed("AC-001"), server::aircraftSeed("AC-001"));
    EXPECT_NE(server::aircraftSeed("AC-001"), server::aircraftSeed("AC-002"));
}

TEST(WeatherMapTest, GenerateWeatherMapCreatesBmpOverOneMegabyte) {
    const std::filesystem::path outputPath = makeTempDirectory("weather_map") / "map.bmp";
    const server::WeatherMapContext context {"AC-101", TelemetryPayload {45.0f, -80.0f, 32000.0f, 450.0f, 90.0f}, true};

    server::generateWeatherMap(outputPath.string(), context, 2);

    ASSERT_TRUE(std::filesystem::exists(outputPath));
    EXPECT_GE(std::filesystem::file_size(outputPath), 1024u * 1024u);

    std::array<char, 54> header {};
    std::ifstream input(outputPath, std::ios::binary);
    input.read(header.data(), static_cast<std::streamsize>(header.size()));
    ASSERT_EQ(input.gcount(), static_cast<std::streamsize>(header.size()));
    EXPECT_EQ(header[0], 'B');
    EXPECT_EQ(header[1], 'M');

    std::uint32_t fileSize = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::memcpy(&fileSize, header.data() + 2, sizeof(fileSize));
    std::memcpy(&width, header.data() + 18, sizeof(width));
    std::memcpy(&height, header.data() + 22, sizeof(height));
    EXPECT_EQ(fileSize, std::filesystem::file_size(outputPath));
    EXPECT_EQ(width, 1024);
    EXPECT_EQ(height, 1024);
}

TEST(WeatherMapTest, GenerateWeatherMapIsDeterministicForSameContextAndChangesWithTelemetry) {
    const std::filesystem::path tempDir = makeTempDirectory("weather_map_compare");
    const std::filesystem::path firstPath = tempDir / "first.bmp";
    const std::filesystem::path secondPath = tempDir / "second.bmp";
    const std::filesystem::path thirdPath = tempDir / "third.bmp";

    const server::WeatherMapContext nominal {"AC-201", TelemetryPayload {45.0f, -81.0f, 30000.0f, 440.0f, 180.0f}, true};
    const server::WeatherMapContext altered {"AC-201", TelemetryPayload {46.0f, -79.0f, 34000.0f, 510.0f, 225.0f}, true};

    server::generateWeatherMap(firstPath.string(), nominal, 5);
    server::generateWeatherMap(secondPath.string(), nominal, 5);
    server::generateWeatherMap(thirdPath.string(), altered, 5);

    const std::string first = readFile(firstPath);
    const std::string second = readFile(secondPath);
    const std::string third = readFile(thirdPath);
    EXPECT_EQ(first, second);
    EXPECT_NE(first, third);
}
