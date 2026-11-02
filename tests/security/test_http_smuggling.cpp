#include <gtest/gtest.h>
#include "http/http_parser.h"

TEST(SecurityTest, RequestSmuggling_ClTeConflict) {
    http::HttpParser parser;
    std::string attack = "POST / HTTP/1.1\r\n"
                         "Host: target.com\r\n"
                         "Content-Length: 13\r\n"
                         "Transfer-Encoding: chunked\r\n\r\n"
                         "0\r\n\r\nSMUGGLED";

    http::HttpRequest req;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_request(attack, req, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::BadRequest);
}

TEST(SecurityTest, RequestSmuggling_DuplicateContentLength) {
    http::HttpParser parser;
    // Same values or different values: all duplicate Content-Length fields must be rejected
    std::string attack = "POST / HTTP/1.1\r\n"
                         "Host: target.com\r\n"
                         "Content-Length: 10\r\n"
                         "Content-Length: 20\r\n\r\n"
                         "1234567890";

    http::HttpRequest req;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_request(attack, req, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::BadRequest);
}

TEST(SecurityTest, RequestSmuggling_MixedCaseDuplicateContentLength) {
    http::HttpParser parser;
    std::string attack = "POST / HTTP/1.1\r\n"
                         "Host: target.com\r\n"
                         "content-length: 5\r\n"
                         "Content-Length: 5\r\n\r\n"
                         "hello";

    http::HttpRequest req;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_request(attack, req, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::BadRequest);
}

TEST(SecurityTest, HeaderInjection_EmbeddedCRLFInValue) {
    http::HttpParser parser;
    // Header value with embedded lone CR (\r) or lone LF (\n) to attempt header injection
    std::string attack = "GET / HTTP/1.1\r\n"
                         "Host: target.com\r\n"
                         "X-Injected: value\reval\r\n\r\n";

    http::HttpRequest req;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_request(attack, req, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::BadRequest);
}

TEST(SecurityTest, HeaderInjection_SpaceInHeaderName) {
    http::HttpParser parser;
    std::string attack = "GET / HTTP/1.1\r\n"
                         "Host: target.com\r\n"
                         "X Injected: value\r\n\r\n";

    http::HttpRequest req;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_request(attack, req, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::BadRequest);
}

TEST(SecurityTest, HostHeaderPoisoning_MultipleHostHeaders) {
    http::HttpParser parser;
    std::string attack = "GET / HTTP/1.1\r\n"
                         "Host: legitimate.com\r\n"
                         "Host: evil.com\r\n\r\n";

    http::HttpRequest req;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_request(attack, req, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::BadRequest);
}

TEST(SecurityTest, HostHeaderPoisoning_MissingHostInHttp11) {
    http::HttpParser parser;
    std::string attack = "GET / HTTP/1.1\r\n"
                         "User-Agent: scanner/1.0\r\n\r\n";

    http::HttpRequest req;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_request(attack, req, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::BadRequest);
}

TEST(SecurityTest, ResourceExhaustion_OversizedHeader) {
    http::HttpParser parser(512, 1024);
    std::string attack = "GET / HTTP/1.1\r\n"
                         "Host: target.com\r\n"
                         "X-Exploit: " + std::string(600, 'A') + "\r\n\r\n";

    http::HttpRequest req;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_request(attack, req, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::RequestHeaderFieldsTooLarge);
}

TEST(SecurityTest, ResourceExhaustion_OversizedBody) {
    http::HttpParser parser(1024, 64);
    std::string attack = "POST /upload HTTP/1.1\r\n"
                         "Host: target.com\r\n"
                         "Content-Length: 500\r\n\r\n" + std::string(500, 'B');

    http::HttpRequest req;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_request(attack, req, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::PayloadTooLarge);
}
