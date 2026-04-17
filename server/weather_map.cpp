#include "weather_map.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace server {
namespace {

const std::string kGeneratedWeatherMapDirectory = "runtime/bitmaps/generated";

std::string fileTimestampUtc() {
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);
    const std::tm* utcNow = std::gmtime(&nowTime);

    std::ostringstream out;
    out << std::put_time(utcNow, "%Y%m%d_%H%M%S")
        << '_'
        << std::setw(3) << std::setfill('0') << milliseconds;
    return out.str();
}

std::string formatSequence(std::uint32_t sequence) {
    std::ostringstream out;
    out << "seq" << std::setw(4) << std::setfill('0') << sequence;
    return out.str();
}

std::uint8_t clampChannel(int value) {
    return static_cast<std::uint8_t>(std::clamp(value, 0, 255));
}

} // namespace

std::string generatedWeatherMapPathFor(const std::string& aircraftId, std::uint32_t transferSequence) {
    return kGeneratedWeatherMapDirectory + "/" + fileTimestampUtc() + "_" + aircraftId + "_" +
           formatSequence(transferSequence) + "_weather_map.bmp";
}

std::uint32_t aircraftSeed(const std::string& aircraftId) {
    std::uint32_t seed = 2166136261u;
    for (const char ch : aircraftId) {
        seed ^= static_cast<std::uint32_t>(static_cast<unsigned char>(ch));
        seed *= 16777619u;
    }
    return seed;
}

void generateWeatherMap(const std::string& path, const WeatherMapContext& context, std::uint32_t transferSequence) {
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());

    constexpr int width = 1024;
    constexpr int height = 1024;
    constexpr int bytesPerPixel = 3;
    constexpr std::uint32_t pixelDataSize = width * height * bytesPerPixel;
    constexpr std::uint32_t fileSize = 54 + pixelDataSize;

    std::array<std::uint8_t, 54> header {};
    header[0] = 'B';
    header[1] = 'M';
    std::memcpy(&header[2], &fileSize, sizeof(fileSize));
    const std::uint32_t pixelOffset = 54;
    std::memcpy(&header[10], &pixelOffset, sizeof(pixelOffset));
    const std::uint32_t dibSize = 40;
    std::memcpy(&header[14], &dibSize, sizeof(dibSize));
    std::int32_t signedWidth = width;
    std::int32_t signedHeight = height;
    std::memcpy(&header[18], &signedWidth, sizeof(signedWidth));
    std::memcpy(&header[22], &signedHeight, sizeof(signedHeight));
    const std::uint16_t planes = 1;
    const std::uint16_t bitCount = 24;
    std::memcpy(&header[26], &planes, sizeof(planes));
    std::memcpy(&header[28], &bitCount, sizeof(bitCount));
    std::memcpy(&header[34], &pixelDataSize, sizeof(pixelDataSize));

    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));

    const std::uint32_t seed = aircraftSeed(context.aircraftId) ^ (transferSequence * 2246822519u);
    int stormCenterX = static_cast<int>(seed % width);
    int stormCenterY = static_cast<int>((seed >> 12) % height);
    int stormRadius = 160 + static_cast<int>(seed % 120);
    int frontOffset = static_cast<int>((seed >> 20) % 220);
    int altitudeBand = 180 + static_cast<int>((seed >> 8) % 320);

    if (context.telemetryValid) {
        stormCenterX = std::clamp(
            static_cast<int>(((context.telemetry.longitude + 180.0f) / 360.0f) * static_cast<float>(width - 1)),
            0,
            width - 1);
        stormCenterY = std::clamp(
            height - 1 -
                static_cast<int>(((context.telemetry.latitude + 90.0f) / 180.0f) * static_cast<float>(height - 1)),
            0,
            height - 1);
        stormRadius = std::clamp(150 + static_cast<int>(context.telemetry.speed * 0.25f), 150, 320);
        frontOffset = static_cast<int>(context.telemetry.heading) % 220;
        altitudeBand = std::clamp(static_cast<int>(context.telemetry.altitude / 120.0f), 120, height - 1);
    }

    const int stormFalloff = (std::max)(1, (stormRadius * stormRadius) / 255);
    std::vector<std::uint8_t> row(static_cast<std::size_t>(width) * bytesPerPixel);
    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t offset = static_cast<std::size_t>(x) * bytesPerPixel;

            const int dx = x - stormCenterX;
            const int dy = y - stormCenterY;
            const int stormIntensity = (std::max)(0, 255 - ((dx * dx + dy * dy) / stormFalloff));
            const int frontBand =
                (std::max)(0, 120 - std::abs((((x + y) + frontOffset) % 240) - 120));
            const int altitudeStripe =
                (std::max)(0, 90 - std::abs((((y * 2) + altitudeBand) % 180) - 90));
            const int turbulence =
                (std::max)(0, 75 - std::abs((((x ^ y) + static_cast<int>(seed & 0xFFu)) % 150) - 75));

            const int blueBase = 85 + ((x * 70) / width) + static_cast<int>((seed >> 16) & 0x1F);
            const int greenBase = 55 + ((y * 65) / height) + static_cast<int>((seed >> 8) & 0x1F);
            const int redBase = 20 + (((x + y) * 30) / (width + height)) + static_cast<int>(seed & 0x1F);

            int blue = blueBase + (stormIntensity / 2) + (frontBand / 5);
            int green = greenBase + (stormIntensity / 4) + (altitudeStripe / 3);
            int red = redBase + (stormIntensity / 7) + (frontBand / 3) + (turbulence / 4);

            if (stormIntensity > 205 && ((x + y + static_cast<int>(seed)) % 97) == 0) {
                red = 250;
                green = 245;
                blue = 220;
            }

            row[offset + 0] = clampChannel(blue);
            row[offset + 1] = clampChannel(green);
            row[offset + 2] = clampChannel(red);
        }
        out.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
    }
}

} // namespace server
