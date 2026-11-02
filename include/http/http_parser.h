#pragma once

#include "http/request.h"
#include "http/response.h"
#include <string>
#include <string_view>
#include <optional>

namespace http {

enum class ParseResult {
    Complete,
    Incomplete,
    Error
};

class HttpParser {
public:
    explicit HttpParser(
        size_t max_header_size = 8192,
        size_t max_body_size = 10 * 1024 * 1024,
        size_t max_header_count = 100
    );

    // Parse incoming client HTTP request incrementally
    ParseResult parse_request(std::string_view data, HttpRequest& out_request, size_t& bytes_consumed);

    // Parse backend HTTP response incrementally
    ParseResult parse_response(
        std::string_view data,
        HttpResponse& out_response,
        size_t& bytes_consumed,
        bool is_eof = false,
        bool is_head_request = false
    );

    [[nodiscard]] StatusCode get_error_status() const noexcept { return error_status_; }
    [[nodiscard]] const std::string& get_error_message() const noexcept { return error_message_; }

    void reset() noexcept;

    // RFC 9110 Section 5.1 / RFC 7230 Section 3.2.6 token character validation
    [[nodiscard]] static bool is_valid_token_char(char c) noexcept;
    [[nodiscard]] static bool is_valid_token(std::string_view token) noexcept;

private:
    size_t max_header_size_;
    size_t max_body_size_;
    size_t max_header_count_;
    StatusCode error_status_{StatusCode::BadRequest};
    std::string error_message_;

    bool parse_request_line(std::string_view line, HttpRequest& req);
    bool parse_headers(std::string_view header_block, HeaderMap& headers, bool is_response = false);
    bool parse_status_line(std::string_view line, HttpResponse& resp);
};

} // namespace http
