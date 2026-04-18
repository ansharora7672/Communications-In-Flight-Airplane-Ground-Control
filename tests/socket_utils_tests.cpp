#include <gtest/gtest.h>

#include <array>
#include <string>
#include <thread>

#include "socket_utils.h"
#include "test_helpers.h"

TEST(SocketUtilsTest, SendAllTransfersEntireBuffer) {
    SocketRuntimeGuard sockets;
    ConnectedSocketPair pair = makeConnectedSocketPair();
    const std::string payload(8192, 'A');
    std::string received(payload.size(), '\0');

    ASSERT_TRUE(sendAll(pair.client, payload.data(), payload.size()));
    ASSERT_TRUE(recvAll(pair.server, received.data(), received.size()));
    EXPECT_EQ(received, payload);
}

TEST(SocketUtilsTest, RecvAllHandlesChunkedPeerWrites) {
    SocketRuntimeGuard sockets;
    ConnectedSocketPair pair = makeConnectedSocketPair();
    std::string received(12, '\0');

    std::thread writer([&pair]() {
        ASSERT_EQ(send(pair.client, "ABCD", 4, 0), 4);
        ASSERT_EQ(send(pair.client, "EFGH", 4, 0), 4);
        ASSERT_EQ(send(pair.client, "IJKL", 4, 0), 4);
    });

    ASSERT_TRUE(recvAll(pair.server, received.data(), received.size()));
    writer.join();
    EXPECT_EQ(received, "ABCDEFGHIJKL");
}

TEST(SocketUtilsTest, WaitForReadableTracksPendingData) {
    SocketRuntimeGuard sockets;
    ConnectedSocketPair pair = makeConnectedSocketPair();
    const char byte = 'Z';

    EXPECT_FALSE(waitForReadable(pair.server, 25));
    ASSERT_EQ(send(pair.client, &byte, 1, 0), 1);
    EXPECT_TRUE(waitForReadable(pair.server, 250));
}

TEST(SocketUtilsTest, RecvAllReturnsFalseWhenPeerClosesEarly) {
    SocketRuntimeGuard sockets;
    ConnectedSocketPair pair = makeConnectedSocketPair();
    const std::array<char, 4> partial {'T', 'E', 'S', 'T'};

    ASSERT_EQ(send(pair.client, partial.data(), static_cast<int>(partial.size()), 0), 4);
    shutdownSocket(pair.client);
    closeSocket(pair.client);
    pair.client = INVALID_SOCK;

    std::array<char, 8> buffer {};
    EXPECT_FALSE(recvAll(pair.server, buffer.data(), buffer.size()));
}

TEST(SocketUtilsTest, ShutdownSocketSendPreservesBufferedBytesBeforeClosure) {
    SocketRuntimeGuard sockets;
    ConnectedSocketPair pair = makeConnectedSocketPair();
    const std::string payload = "DONE";
    std::array<char, 4> received {};

    ASSERT_EQ(send(pair.client, payload.data(), static_cast<int>(payload.size()), 0), 4);
    shutdownSocketSend(pair.client);

    ASSERT_TRUE(recvAll(pair.server, received.data(), received.size()));
    EXPECT_EQ(std::string(received.data(), received.size()), payload);

    char extra = '\0';
    EXPECT_EQ(recv(pair.server, &extra, 1, 0), 0);
}
