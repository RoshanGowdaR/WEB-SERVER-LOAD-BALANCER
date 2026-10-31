#include <gtest/gtest.h>
#include "http/http_parser.h"
#include "server/server.h"
#include "network/socket.h"

TEST(EdgeCasesTest, RejectUnsupportedMethod) {
    http::HttpParser parser;
    std::string raw = "TRACE /secret HTTP/1.1\r\nHost: localhost\r\n\r\n";
    http::HttpRequest req;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_request(raw, req, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::MethodNotAllowed);
}

TEST(EdgeCasesTest, HandleZeroLengthBodyPost) {
    http::HttpParser parser;
    std::string raw = "POST /empty HTTP/1.1\r\n"
                      "Host: localhost\r\n"
                      "Content-Length: 0\r\n\r\n";
    http::HttpRequest req;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_request(raw, req, consumed), http::ParseResult::Complete);
    EXPECT_EQ(req.content_length(), 0);
    EXPECT_TRUE(req.body.empty());
}

TEST(EdgeCasesTest, AbruptClientDisconnectHandledGracefully) {
    config::ServerConfig cfg;
    cfg.host = "127.0.0.1";
    cfg.port = 18089;
    cfg.worker_threads = 2;
    cfg.max_queue_size = 50;
    cfg.backends = {{"127.0.0.1", 19992, 1}};

    server::Server srv(cfg);
    ASSERT_TRUE(srv.start());

    // Connect and immediately close socket without sending any data
    {
        net::ClientSocket client;
        ASSERT_TRUE(client.connect("127.0.0.1", 18089, std::chrono::milliseconds(1000)));
        client.close();
    }

    // Server should still be alive and responsive to subsequent requests
    net::ClientSocket client2;
    ASSERT_TRUE(client2.connect("127.0.0.1", 18089, std::chrono::milliseconds(1000)));
    std::string req = "GET /health HTTP/1.1\r\nHost: 127.0.0.1:18089\r\nConnection: close\r\n\r\n";
    EXPECT_GT(client2.send_string(req), 0);

    char buf[512];
    ssize_t bytes = client2.receive(buf, sizeof(buf) - 1);
    EXPECT_GT(bytes, 0);

    srv.stop();
}
