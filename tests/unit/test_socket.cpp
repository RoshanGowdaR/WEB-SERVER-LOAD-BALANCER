#include <gtest/gtest.h>
#include "network/socket.h"
#include <thread>
#include <chrono>

TEST(SocketTest, DefaultSocketIsInvalid) {
    net::Socket sock;
    EXPECT_FALSE(sock.is_valid());
    EXPECT_EQ(sock.get_fd(), -1);
}

TEST(SocketTest, MoveSemanticsTransferOwnership) {
    net::ServerSocket server;
    // Bind to ephemeral port
    ASSERT_TRUE(server.bind_and_listen("127.0.0.1", 0));
    EXPECT_TRUE(server.is_valid());
    int original_fd = server.get_fd();

    net::Socket moved(std::move(server));
    EXPECT_TRUE(moved.is_valid());
    EXPECT_EQ(moved.get_fd(), original_fd);
    EXPECT_FALSE(server.is_valid());
    EXPECT_EQ(server.get_fd(), -1);
}

TEST(SocketTest, SocketOptionsConfiguration) {
    net::ServerSocket server;
    ASSERT_TRUE(server.bind_and_listen("127.0.0.1", 0));
    EXPECT_TRUE(server.set_reuse_address(true));
    EXPECT_TRUE(server.set_reuse_port(true));
    EXPECT_TRUE(server.set_non_blocking(true));
    EXPECT_TRUE(server.set_tcp_nodelay(true));
    EXPECT_TRUE(server.set_receive_timeout(std::chrono::milliseconds(500)));
    EXPECT_TRUE(server.set_send_timeout(std::chrono::milliseconds(500)));
}

TEST(SocketTest, ClientServerCommunication) {
    net::ServerSocket server;
    ASSERT_TRUE(server.bind_and_listen("127.0.0.1", 0));
    uint16_t port = server.get_bound_port();

    std::string sent_msg = "HELLO_TCP_SERVER";
    std::string received_msg;

    std::thread server_thread([&]() {
        std::string client_ip;
        uint16_t client_port = 0;
        net::Socket client = server.accept(&client_ip, &client_port);
        ASSERT_TRUE(client.is_valid());
        EXPECT_FALSE(client_ip.empty());
        EXPECT_GT(client_port, 0);

        char buf[64];
        ssize_t bytes = client.receive(buf, sizeof(buf) - 1);
        if (bytes > 0) {
            buf[bytes] = '\0';
            received_msg = buf;
        }

        std::string reply = "ACK";
        client.send_string(reply);
    });

    net::ClientSocket client;
    ASSERT_TRUE(client.connect("127.0.0.1", port, std::chrono::milliseconds(2000)));
    EXPECT_EQ(client.send_string(sent_msg), static_cast<ssize_t>(sent_msg.size()));

    char ack_buf[16];
    ssize_t ack_bytes = client.receive(ack_buf, sizeof(ack_buf) - 1);
    ASSERT_GT(ack_bytes, 0);
    ack_buf[ack_bytes] = '\0';
    EXPECT_STREQ(ack_buf, "ACK");

    server_thread.join();
    EXPECT_EQ(received_msg, sent_msg);
}
