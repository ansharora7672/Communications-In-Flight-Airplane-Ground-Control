#include <gtest/gtest.h>

#include <cstring>

#include "packet.h"

TEST(PacketHeaderTest, SizeIsFixed) {
    // REQ-PKT-010: Packet headers shall have a fixed layout.
    EXPECT_EQ(sizeof(PacketHeader), 29u);
}

TEST(PacketHeaderTest, MakeHeaderSetsAllFields) {
    // REQ-PKT-020: Header includes packet type, aircraft ID, sequence number, and payload size.
    PacketHeader header = makeHeader(PacketType::TELEMETRY, "AC-001", 1, sizeof(TelemetryPayload), 0);

    EXPECT_EQ(header.packet_type, PacketType::TELEMETRY);
    EXPECT_EQ(header.sequence_number, 1u);
    EXPECT_EQ(header.payload_size, sizeof(TelemetryPayload));
    EXPECT_EQ(extractAircraftId(header.aircraft_id), "AC-001");
}

TEST(PacketHeaderTest, MakeHeaderStoresChecksum) {
    // REQ-PKT-070: Header stores the calculated checksum value for integrity checks.
    PacketHeader header = makeHeader(PacketType::TELEMETRY, "AC-001", 7, sizeof(TelemetryPayload), 1234);

    EXPECT_EQ(header.checksum, 1234u);
}

TEST(PacketHeaderTest, MakeHeaderAircraftIdTruncatesAt15Chars) {
    // REQ-PKT-020: Aircraft ID field remains null-terminated in its fixed 16-byte buffer.
    PacketHeader header = makeHeader(PacketType::TELEMETRY, "AC-VERYLONGIDTHATEXCEEDS", 1, 0, 0);

    EXPECT_EQ(header.aircraft_id[15], '\0');
}

TEST(PacketHeaderTest, MakeHeaderZeroFillsUnusedAircraftIdBytes) {
    // REQ-PKT-020: Fixed aircraft ID field is padded safely after the identifier.
    PacketHeader header = makeHeader(PacketType::TELEMETRY, "AC-1", 1, 0, 0);

    EXPECT_EQ(header.aircraft_id[4], '\0');
    EXPECT_EQ(header.aircraft_id[15], '\0');
}

TEST(PacketHeaderTest, ExtractAircraftIdReturnsCorrectString) {
    // REQ-PKT-020: Receiver can extract the aircraft identifier from the header.
    char id[16] = "AC-042";

    EXPECT_EQ(extractAircraftId(id), "AC-042");
}

TEST(PacketHeaderTest, ExtractAircraftIdHandlesFullBufferWithoutNullTerminator) {
    // REQ-PKT-020: Extraction is bounded by the 16-byte aircraft ID field.
    char id[16];
    std::memcpy(id, "ABCDEFGHIJKLMNOP", sizeof(id));

    EXPECT_EQ(extractAircraftId(id), "ABCDEFGHIJKLMNOP");
}

TEST(PacketHeaderTest, ExtractAircraftIdHandlesEmptyId) {
    // REQ-PKT-020: Empty aircraft ID buffers are handled safely.
    char id[16];
    std::memset(id, 0, sizeof(id));

    EXPECT_EQ(extractAircraftId(id), "");
}

TEST(PacketHeaderTest, PacketTypeToStringAllTypes) {
    // REQ-PKT-010: Packet type identifiers have deterministic string representations.
    EXPECT_EQ(packetTypeToString(PacketType::HANDSHAKE_REQUEST), "HANDSHAKE_REQUEST");
    EXPECT_EQ(packetTypeToString(PacketType::HANDSHAKE_ACK), "HANDSHAKE_ACK");
    EXPECT_EQ(packetTypeToString(PacketType::HANDSHAKE_FAIL), "HANDSHAKE_FAIL");
    EXPECT_EQ(packetTypeToString(PacketType::TELEMETRY), "TELEMETRY");
    EXPECT_EQ(packetTypeToString(PacketType::LARGE_FILE), "LARGE_FILE");
    EXPECT_EQ(packetTypeToString(PacketType::DISCONNECT), "DISCONNECT");
}
