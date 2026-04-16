#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

#include "packet.h"

TEST(ChecksumTest, EmptyBuffer) {
    // REQ-PKT-070: Empty payloads produce a zero checksum.
    EXPECT_EQ(computeChecksum(nullptr, 0), 0u);
}

TEST(ChecksumTest, KnownValues) {
    // REQ-PKT-070: Checksum equals the sum of payload bytes.
    std::uint8_t data[] = {0x01, 0x02, 0x03, 0x04};

    EXPECT_EQ(computeChecksum(data, 4), 10u);
}

TEST(ChecksumTest, SingleByte) {
    // REQ-PKT-070: Single-byte payload checksums are calculated correctly.
    std::uint8_t data[] = {0xFF};

    EXPECT_EQ(computeChecksum(data, 1), 255u);
}

TEST(ChecksumTest, Overflow) {
    // REQ-PKT-070: Checksum uses uint32 arithmetic for accumulated payload bytes.
    std::uint8_t data[256];
    std::memset(data, 0xFF, sizeof(data));

    EXPECT_EQ(computeChecksum(data, static_cast<std::uint32_t>(sizeof(data))), 65280u);
}
