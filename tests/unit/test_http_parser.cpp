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
    std::string raw = "GET /test HTTP/1.1\r\nHost: localhost\r\nX-Large-Header: " + large_header + "\r\n\r\n";

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

TEST(HttpParserTest, RejectHttp11WithoutHostHeader) {
    http::HttpParser parser;
    std::string raw = "GET /index.html HTTP/1.1\r\nUser-Agent: test\r\n\r\n";

    http::HttpRequest req;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_request(raw, req, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::BadRequest);
    EXPECT_NE(parser.get_error_message().find("Host"), std::string::npos);
}

TEST(HttpParserTest, RejectMultipleHostHeaders) {
    http::HttpParser parser;
    std::string raw = "GET /index.html HTTP/1.1\r\n"
                      "Host: example.com\r\n"
                      "Host: attacker.com\r\n\r\n";

    http::HttpRequest req;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_request(raw, req, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::BadRequest);
}

TEST(HttpParserTest, RejectInvalidContentLengthFormats) {
    http::HttpParser parser;
    http::HttpRequest req;
    size_t consumed = 0;

    // Signed positive
    std::string raw1 = "POST /test HTTP/1.1\r\nHost: local\r\nContent-Length: +5\r\n\r\nhello";
    EXPECT_EQ(parser.parse_request(raw1, req, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::BadRequest);

    // Negative
    std::string raw2 = "POST /test HTTP/1.1\r\nHost: local\r\nContent-Length: -5\r\n\r\nhello";
    parser.reset();
    EXPECT_EQ(parser.parse_request(raw2, req, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::BadRequest);

    // Non-numeric trailing characters
    std::string raw3 = "POST /test HTTP/1.1\r\nHost: local\r\nContent-Length: 5abc\r\n\r\nhello";
    parser.reset();
    EXPECT_EQ(parser.parse_request(raw3, req, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::BadRequest);
}

TEST(HttpParserTest, RejectConflictingContentLengthAndTransferEncoding) {
    http::HttpParser parser;
    std::string raw = "POST /smuggle HTTP/1.1\r\n"
                      "Host: test\r\n"
                      "Transfer-Encoding: chunked\r\n"
                      "Content-Length: 20\r\n\r\n";

    http::HttpRequest req;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_request(raw, req, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::BadRequest);
}

TEST(HttpParserTest, RejectDuplicateContentLengthEvenIfIdentical) {
    http::HttpParser parser;
    std::string raw = "POST /submit HTTP/1.1\r\n"
                      "Host: test\r\n"
                      "Content-Length: 20\r\n"
                      "Content-Length: 20\r\n\r\n";

    http::HttpRequest req;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_request(raw, req, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::BadRequest);
}

TEST(HttpParserTest, RejectInvalidHeaderFieldNames) {
    http::HttpParser parser;
    // Header name with illegal space
    std::string raw = "GET /test HTTP/1.1\r\nHost: local\r\nBad Header: value\r\n\r\n";

    http::HttpRequest req;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_request(raw, req, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::BadRequest);
}

TEST(HttpParserTest, RejectCrlfInjectionInHeaderValue) {
    http::HttpParser parser;
    // Value with embedded CR
    std::string raw = "GET /test HTTP/1.1\r\nHost: local\r\nX-Injected: line1\rline2\r\n\r\n";

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

TEST(HttpParserTest, ParseBackendResponseSpecialStatusCodesWithoutBody) {
    http::HttpParser parser;
    // 204 No Content
    std::string raw204 = "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n";
    http::HttpResponse resp204;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_response(raw204, resp204, consumed), http::ParseResult::Complete);
    EXPECT_EQ(resp204.status_code, http::StatusCode::NoContent);
    EXPECT_TRUE(resp204.body.empty());

    // 304 Not Modified
    std::string raw304 = "HTTP/1.1 304 Not Modified\r\nETag: \"abc\"\r\n\r\n";
    http::HttpResponse resp304;
    consumed = 0;
    EXPECT_EQ(parser.parse_response(raw304, resp304, consumed), http::ParseResult::Complete);
    EXPECT_EQ(resp304.status_code, http::StatusCode::NotModified);
    EXPECT_TRUE(resp304.body.empty());
}

TEST(HttpParserTest, RejectBackendResponseOversizedBody) {
    http::HttpParser parser(4096, 50); // max 50 bytes body
    std::string raw = "HTTP/1.1 200 OK\r\nContent-Length: 1000\r\n\r\n" + std::string(1000, 'X');
    http::HttpResponse resp;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_response(raw, resp, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::BadGateway);
}

TEST(HttpParserTest, ParseBackendResponseInformational1xx) {
    http::HttpParser parser;
    // 100 Continue
    std::string raw100 = "HTTP/1.1 100 Continue\r\n\r\n";
    http::HttpResponse resp100;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_response(raw100, resp100, consumed), http::ParseResult::Complete);
    EXPECT_EQ(resp100.status_code, http::StatusCode::Continue);
    EXPECT_EQ(consumed, raw100.size());
    EXPECT_TRUE(resp100.body.empty());

    // 103 Early Hints
    std::string raw103 = "HTTP/1.1 103 Early Hints\r\nLink: </style.css>; rel=preload\r\n\r\n";
    http::HttpResponse resp103;
    consumed = 0;
    EXPECT_EQ(parser.parse_response(raw103, resp103, consumed), http::ParseResult::Complete);
    EXPECT_EQ(resp103.status_code, http::StatusCode::EarlyHints);
    EXPECT_EQ(consumed, raw103.size());
    EXPECT_TRUE(resp103.body.empty());
}

TEST(HttpParserTest, ParseBackendChunkedTransferEncoding) {
    http::HttpParser parser;
    std::string raw = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n";
    http::HttpResponse resp;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_response(raw, resp, consumed), http::ParseResult::Complete);
    EXPECT_EQ(resp.body, "hello");
    EXPECT_EQ(resp.get_header("Content-Length").value_or(""), "5");
    EXPECT_FALSE(resp.has_header("Transfer-Encoding"));
    EXPECT_EQ(consumed, raw.size());
}

TEST(HttpParserTest, ParseRequestChunkedTransferEncoding) {
    http::HttpParser parser;
    std::string raw = "POST /api/upload HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n"
                      "4\r\nWiki\r\n"
                      "6\r\npedia \r\n"
                      "0\r\n\r\n";
    http::HttpRequest req;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_request(raw, req, consumed), http::ParseResult::Complete);
    EXPECT_EQ(req.body, "Wikipedia ");
    EXPECT_EQ(req.get_header("Content-Length").value_or(""), "10");
    EXPECT_FALSE(req.has_header("Transfer-Encoding"));
    EXPECT_EQ(consumed, raw.size());
}

TEST(HttpParserTest, ParseChunkedWithExtensionsAndTrailers) {
    http::HttpParser parser;
    std::string raw = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
                      "5;ext=foo;bar=baz\r\nhello\r\n"
                      "0\r\n"
                      "Expires: Wed, 21 Oct 2025 07:28:00 GMT\r\n"
                      "X-Custom-Trailer: valid-trailer\r\n"
                      "\r\n";
    http::HttpResponse resp;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_response(raw, resp, consumed), http::ParseResult::Complete);
    EXPECT_EQ(resp.body, "hello");
    EXPECT_EQ(resp.get_header("Expires").value_or(""), "Wed, 21 Oct 2025 07:28:00 GMT");
    EXPECT_EQ(resp.get_header("X-Custom-Trailer").value_or(""), "valid-trailer");
    EXPECT_EQ(resp.get_header("Content-Length").value_or(""), "5");
    EXPECT_FALSE(resp.has_header("Transfer-Encoding"));
}

TEST(HttpParserTest, RejectForbiddenTrailersInChunkedEncoding) {
    http::HttpParser parser;
    // Trailer containing forbidden Transfer-Encoding
    std::string raw_te = "POST /data HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n"
                         "4\r\ntest\r\n0\r\nTransfer-Encoding: chunked\r\n\r\n";
    http::HttpRequest req;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_request(raw_te, req, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::BadRequest);

    // Trailer containing forbidden Content-Length
    std::string raw_cl = "POST /data HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n"
                         "4\r\ntest\r\n0\r\nContent-Length: 4\r\n\r\n";
    parser.reset();
    EXPECT_EQ(parser.parse_request(raw_cl, req, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::BadRequest);
}

TEST(HttpParserTest, RejectMalformedChunkSize) {
    http::HttpParser parser;
    // Non-hex chunk size
    std::string raw = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\nhello\r\n0\r\n\r\n";
    http::HttpResponse resp;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_response(raw, resp, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::BadGateway);
}

TEST(HttpParserTest, RejectOversizedAggregateChunkedBody) {
    http::HttpParser parser(8192, 10); // max body size 10 bytes
    std::string raw = "POST /upload HTTP/1.1\r\nHost: localhost\r\nTransfer-Encoding: chunked\r\n\r\n"
                      "8\r\n12345678\r\n"
                      "8\r\n12345678\r\n"
                      "0\r\n\r\n";
    http::HttpRequest req;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_request(raw, req, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::PayloadTooLarge);
}

TEST(HttpParserTest, RejectUnsupportedExpectHeaderWith417) {
    http::HttpParser parser;
    std::string raw = "POST /upload HTTP/1.1\r\nHost: localhost\r\nExpect: 200-ok\r\nContent-Length: 5\r\n\r\nhello";
    http::HttpRequest req;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_request(raw, req, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::ExpectationFailed);
}

TEST(HttpParserTest, AcceptExpect100ContinueHeaderBeforeBody) {
    http::HttpParser parser;
    std::string raw = "POST /upload HTTP/1.1\r\nHost: localhost\r\nExpect: 100-continue\r\nContent-Length: 100\r\n\r\n";
    http::HttpRequest req;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_request(raw, req, consumed), http::ParseResult::Complete);
    EXPECT_TRUE(req.expects_100_continue());
    EXPECT_EQ(req.content_length(), 100u);
    EXPECT_TRUE(req.body.empty());
    EXPECT_EQ(consumed, raw.size());
}

TEST(HttpParserTest, RejectBackendUnsupportedTransferEncoding) {
    http::HttpParser parser;
    std::string raw = "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\n";
    http::HttpResponse resp;
    size_t consumed = 0;
    EXPECT_EQ(parser.parse_response(raw, resp, consumed), http::ParseResult::Error);
    EXPECT_EQ(parser.get_error_status(), http::StatusCode::BadGateway);
}
