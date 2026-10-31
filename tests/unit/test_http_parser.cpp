#include <gtest/gtest.h>
#include "http/http_parser.h"

TEST(HttpParserTest, ParseValidGetRequest) {
    http::HttpParser parser;
    std::string raw = "GET /api/v1/users?page=2&limit=10 HTTP/1.1\r\n"
                      "Host: localhost:8080\r\n"
                      "User-Agent: curl/7.88.1\r\n"
                      "Accept: application/json\r\n"
                      "Connection: keep-alive\r\n\r\n";

    http::HttpRequest req;
    size_t consumed = 0;
    http::ParseResult res = parser.parse_request(raw, req, consumed);

    EXPECT_EQ(res, http::ParseResult::Complete);
    EXPECT_EQ(consumed, raw.size());
    EXPECT_EQ(req.method, http::Method::GET);
    EXPECT_EQ(req.path, "/api/v1/users");
    EXPECT_EQ(req.query, "page=2&limit=10");
    EXPECT_EQ(req.version, "HTTP/1.1");
    EXPECT_TRUE(req.has_header("host"));
    EXPECT_EQ(req.get_header("Host").value_or(""), "localhost:8080");
    EXPECT_TRUE(req.is_keep_alive());
    EXPECT_TRUE(req.body.empty());
}

TEST(HttpParserTest, ParseValidPostRequestWithBody) {
    http::HttpParser parser;
    std::string body = "{\"name\":\"Antigravity\",\"role\":\"Systems Architect\"}";
    std::string raw = "POST /api/v1/create HTTP/1.1\r\n"
                      "Host: 127.0.0.1\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: " + std::to_string(body.size()) + "\r\n"
                      "\r\n" + body;

    http::HttpRequest req;
    size_t consumed = 0;
    http::ParseResult res = parser.parse_request(raw, req, consumed);

    EXPECT_EQ(res, http::ParseResult::Complete);
    EXPECT_EQ(consumed, raw.size());
    EXPECT_EQ(req.method, http::Method::POST);
    EXPECT_EQ(req.path, "/api/v1/create");
    EXPECT_EQ(req.content_length(), body.size());
    EXPECT_EQ(req.body, body);
}

TEST(HttpParserTest, IncrementalStreamingParse) {
    http::HttpParser parser;
    std::string chunk1 = "GET /index.html HTTP/1.1\r\nHost: example.com\r\n";
    std::string chunk2 = "Connection: close\r\n\r\n";

    http::HttpRequest req;
    size_t consumed = 0;

    // First chunk incomplete
    EXPECT_EQ(parser.parse_request(chunk1, req, consumed), http::ParseResult::Incomplete);

    // Combined chunks complete
    std::string combined = chunk1 + chunk2;
    EXPECT_EQ(parser.parse_request(combined, req, consumed), http::ParseResult::Complete);
    EXPECT_EQ(consumed, combined.size());
    EXPECT_EQ(req.path, "/index.html");
    EXPECT_FALSE(req.is_keep_alive());
}

TEST(HttpParserTest, RejectOversizedHeaders) {
    http::HttpParser parser(256, 1024); // Low 256 bytes header limit
    std::string large_header(300, 'X');
    std::string raw = "GET /test HTTP/1.1\r\nX-Large-Header: " + large_header + "\r\n\r\n";

    http::HttpRequest req;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_request(raw, req, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::RequestHeaderFieldsTooLarge);
}

TEST(HttpParserTest, RejectOversizedBody) {
    http::HttpParser parser(4096, 50); // Max 50 byte body
    std::string raw = "POST /upload HTTP/1.1\r\n"
                      "Host: localhost\r\n"
                      "Content-Length: 100\r\n\r\n" + std::string(100, 'A');

    http::HttpRequest req;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_request(raw, req, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::PayloadTooLarge);
}

TEST(HttpParserTest, RejectMalformedRequestLine) {
    http::HttpParser parser;
    std::string raw = "INVALID_REQUEST_WITHOUT_SPACES\r\n\r\n";

    http::HttpRequest req;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_request(raw, req, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::BadRequest);
}

TEST(HttpParserTest, RejectConflictingContentLength) {
    http::HttpParser parser;
    std::string raw = "POST /submit HTTP/1.1\r\n"
                      "Host: test\r\n"
                      "Content-Length: 20\r\n"
                      "Content-Length: 40\r\n\r\n";

    http::HttpRequest req;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_request(raw, req, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::BadRequest);
}

TEST(HttpParserTest, ParseBackendResponse) {
    http::HttpParser parser;
    std::string body = "{\"backend\":\"node-1\",\"uptime\":3600}";
    std::string raw = "HTTP/1.1 200 OK\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: " + std::to_string(body.size()) + "\r\n"
                      "\r\n" + body;

    http::HttpResponse resp;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_response(raw, resp, consumed), http::ParseResult::Complete);
    EXPECT_EQ(resp.status_code, http::StatusCode::OK);
    EXPECT_EQ(resp.body, body);
}
