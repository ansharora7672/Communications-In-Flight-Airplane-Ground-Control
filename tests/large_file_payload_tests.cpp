#include <gtest/gtest.h>

#include <cstdlib>
#include <utility>

#include "packet.h"

TEST(LargeFilePayloadTest, DefaultConstructor) {
    // REQ-PKT-030: Dynamic payload storage starts empty.
    LargeFilePayload payload;

    EXPECT_EQ(payload.data, nullptr);
    EXPECT_EQ(payload.size, 0u);
}

TEST(LargeFilePayloadTest, MoveConstructor) {
    // REQ-PKT-030, REQ-CLT-060: Move construction transfers ownership and prevents double-free.
    LargeFilePayload original;
    original.data = static_cast<std::uint8_t*>(std::malloc(1024));
    ASSERT_NE(original.data, nullptr);
    original.size = 1024;

    LargeFilePayload moved(std::move(original));

    EXPECT_NE(moved.data, nullptr);
    EXPECT_EQ(moved.size, 1024u);
    EXPECT_EQ(original.data, nullptr);
    EXPECT_EQ(original.size, 0u);
}

TEST(LargeFilePayloadTest, MoveAssignment) {
    // REQ-PKT-030, REQ-CLT-060: Move assignment transfers ownership and releases old storage.
    LargeFilePayload original;
    original.data = static_cast<std::uint8_t*>(std::malloc(1024));
    ASSERT_NE(original.data, nullptr);
    original.size = 1024;

    LargeFilePayload moved;
    moved = std::move(original);

    EXPECT_NE(moved.data, nullptr);
    EXPECT_EQ(moved.size, 1024u);
    EXPECT_EQ(original.data, nullptr);
    EXPECT_EQ(original.size, 0u);
}

TEST(LargeFilePayloadTest, DestructorFreesMemory) {
    // REQ-PKT-050, REQ-CLT-060: Destructor releases a 1 MB large-transfer buffer.
    auto* payload = new LargeFilePayload();
    payload->data = static_cast<std::uint8_t*>(std::malloc(1048576));
    ASSERT_NE(payload->data, nullptr);
    payload->size = 1048576;

    delete payload;
    SUCCEED();
}
