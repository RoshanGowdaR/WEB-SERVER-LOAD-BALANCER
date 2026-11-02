#include "http/http_parser.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <limits>

namespace http {

namespace {

std::string_view trim_whitespace(std::string_view str) {
    while (!str.empty() && (str.front() == ' ' || str.front() == '\t')) {
        str.remove_prefix(1);
    }
    while (!str.empty() && (str.back() == ' ' || str.back() == '\t')) {
        str.remove_suffix(1);
    }
    return str;
}

std::string_view trim_crlf(std::string_view str) {
    while (!str.empty() && (str.back() == '\r' || str.back() == '\n')) {
        str.remove_suffix(1);
    }
    return str;
}

bool contains_control_chars(std::string_view str) {
    for (unsigned char c : str) {
        if (c < 0x20 || c == 0x7F) {
            return true;
        }
    }
    return false;
}

bool contains_crlf(std::string_view str) {
    for (char c : str) {
        if (c == '\r' || c == '\n') {
            return true;
        }
    }
    return false;
}

} // namespace

bool HttpParser::is_valid_token_char(char c) noexcept {
    unsigned char uc = static_cast<unsigned char>(c);
    if ((uc >= 'a' && uc <= 'z') || (uc >= 'A' && uc <= 'Z') || (uc >= '0' && uc <= '9')) {
        return true;
    }
    switch (c) {
        case '!': case '#': case '$': case '%': case '&':
        case '\'': case '*': case '+': case '-': case '.':
        case '^': case '_': case '`': case '|': case '~':
            return true;
        default:
            return false;
    }
}

bool HttpParser::is_valid_token(std::string_view token) noexcept {
    if (token.empty()) return false;
    for (char c : token) {
        if (!is_valid_token_char(c)) return false;
    }
    return true;
}

HttpParser::HttpParser(size_t max_header_size, size_t max_body_size, size_t max_header_count)
    : max_header_size_(max_header_size),
      max_body_size_(max_body_size),
      max_header_count_(max_header_count) {}

void HttpParser::reset() noexcept {
    error_status_ = StatusCode::BadRequest;
    error_message_.clear();
}

ParseResult HttpParser::parse_request(std::string_view data, HttpRequest& out_request, size_t& bytes_consumed) {
    bytes_consumed = 0;

    // Search for end of headers: "\r\n\r\n" or "\n\n"
    size_t header_end = data.find("\r\n\r\n");
    size_t delimiter_len = 4;
    if (header_end == std::string_view::npos) {
        header_end = data.find("\n\n");
        delimiter_len = 2;
    }

    if (header_end == std::string_view::npos) {
        if (data.size() > max_header_size_) {
            error_status_ = StatusCode::RequestHeaderFieldsTooLarge;
            error_message_ = "Request headers exceeded maximum allowed size.";
            return ParseResult::Error;
        }
        return ParseResult::Incomplete;
    }

    if (header_end > max_header_size_) {
        error_status_ = StatusCode::RequestHeaderFieldsTooLarge;
        error_message_ = "Request headers exceeded maximum allowed size.";
        return ParseResult::Error;
    }

    std::string_view header_section = data.substr(0, header_end);
    size_t line_end = header_section.find("\r\n");
    size_t line_delim = 2;
    if (line_end == std::string_view::npos) {
        line_end = header_section.find('\n');
        line_delim = 1;
    }

    if (line_end == std::string_view::npos) {
        error_status_ = StatusCode::BadRequest;
        error_message_ = "Malformed request: missing request line delimiter.";
        return ParseResult::Error;
    }

    std::string_view request_line = header_section.substr(0, line_end);
    if (!parse_request_line(request_line, out_request)) {
        return ParseResult::Error;
    }

    std::string_view headers_block = header_section.substr(line_end + line_delim);
    out_request.headers.clear();
    if (!parse_headers(headers_block, out_request.headers, false)) {
        return ParseResult::Error;
    }

    // RFC 9112 Section 3.2: HTTP/1.1 requests must have a Host header
    if (out_request.version == "HTTP/1.1") {
        if (!out_request.has_header("Host")) {
            error_status_ = StatusCode::BadRequest;
            error_message_ = "Missing required Host header in HTTP/1.1 request.";
            return ParseResult::Error;
        }
        auto host_val = out_request.get_header("Host").value_or("");
        if (host_val.empty() || contains_control_chars(host_val) || contains_crlf(host_val)) {
            error_status_ = StatusCode::BadRequest;
            error_message_ = "Invalid Host header value.";
            return ParseResult::Error;
        }
    }

    // Framing verification: Conflicting Transfer-Encoding and Content-Length is request smuggling risk
    bool has_te = out_request.has_header("Transfer-Encoding");
    bool has_cl = out_request.has_header("Content-Length");

    if (has_te && has_cl) {
        error_status_ = StatusCode::BadRequest;
        error_message_ = "Conflicting framing: Both Transfer-Encoding and Content-Length present.";
        return ParseResult::Error;
    }

    if (has_te) {
        auto te = out_request.get_header("Transfer-Encoding").value_or("");
        if (!te.empty() && te != "identity") {
            error_status_ = StatusCode::NotImplemented;
            error_message_ = "Unsupported Transfer-Encoding; chunked transfer encoding is not supported.";
            return ParseResult::Error;
        }
    }

    size_t content_length = 0;
    if (has_cl) {
        auto cl_str = out_request.get_header("Content-Length").value_or("");
        cl_str = std::string(trim_whitespace(cl_str));

        if (cl_str.empty()) {
            error_status_ = StatusCode::BadRequest;
            error_message_ = "Empty Content-Length header value.";
            return ParseResult::Error;
        }

        // Strict numeric parsing using std::from_chars (no sign, no trailing characters)
        size_t parsed_len = 0;
        auto [ptr, ec] = std::from_chars(cl_str.data(), cl_str.data() + cl_str.size(), parsed_len);
        if (ec != std::errc{} || ptr != cl_str.data() + cl_str.size()) {
            error_status_ = StatusCode::BadRequest;
            error_message_ = "Invalid Content-Length header value: non-numeric or out-of-range.";
            return ParseResult::Error;
        }

        if (parsed_len > max_body_size_) {
            error_status_ = StatusCode::PayloadTooLarge;
            error_message_ = "Request payload exceeds configured limit.";
            return ParseResult::Error;
        }

        content_length = parsed_len;
    }

    // Safe integer arithmetic check against overflow
    if (content_length > std::numeric_limits<size_t>::max() - (header_end + delimiter_len)) {
        error_status_ = StatusCode::BadRequest;
        error_message_ = "Content-Length integer overflow detected.";
        return ParseResult::Error;
    }

    size_t total_required = header_end + delimiter_len + content_length;
    if (data.size() < total_required) {
        return ParseResult::Incomplete;
    }

    if (content_length > 0) {
        out_request.body = std::string(data.substr(header_end + delimiter_len, content_length));
    } else {
        out_request.body.clear();
    }

    bytes_consumed = total_required;
    return ParseResult::Complete;
}

bool HttpParser::parse_request_line(std::string_view line, HttpRequest& req) {
    line = trim_crlf(line);
    if (line.empty()) {
        error_status_ = StatusCode::BadRequest;
        error_message_ = "Empty request line.";
        return false;
    }

    // Expected: METHOD SP URI SP VERSION
    size_t first_sp = line.find(' ');
    if (first_sp == std::string_view::npos || first_sp == 0) {
        error_status_ = StatusCode::BadRequest;
        error_message_ = "Malformed request line: missing space after method.";
        return false;
    }

    std::string_view method_part = line.substr(0, first_sp);
    if (!is_valid_token(method_part)) {
        error_status_ = StatusCode::BadRequest;
        error_message_ = "Invalid HTTP method token.";
        return false;
    }

    req.method = string_to_method(method_part);
    if (req.method == Method::UNKNOWN) {
        error_status_ = StatusCode::MethodNotAllowed;
        error_message_ = "Unsupported or invalid HTTP method: " + std::string(method_part);
        return false;
    }

    size_t second_sp = line.find(' ', first_sp + 1);
    if (second_sp == std::string_view::npos) {
        error_status_ = StatusCode::BadRequest;
        error_message_ = "Malformed request line: missing HTTP version.";
        return false;
    }

    // Ensure no third space (strictly 3 components)
    if (line.find(' ', second_sp + 1) != std::string_view::npos) {
        error_status_ = StatusCode::BadRequest;
        error_message_ = "Malformed request line: excess tokens.";
        return false;
    }

    std::string_view uri_part = line.substr(first_sp + 1, second_sp - (first_sp + 1));
    if (uri_part.empty() || uri_part[0] != '/') {
        error_status_ = StatusCode::BadRequest;
        error_message_ = "Invalid request URI: must start with '/' (origin-form).";
        return false;
    }

    if (contains_control_chars(uri_part) || contains_crlf(uri_part)) {
        error_status_ = StatusCode::BadRequest;
        error_message_ = "URI contains forbidden control characters.";
        return false;
    }

    req.uri = std::string(uri_part);

    size_t qpos = req.uri.find('?');
    if (qpos != std::string::npos) {
        req.path = req.uri.substr(0, qpos);
        req.query = req.uri.substr(qpos + 1);
    } else {
        req.path = req.uri;
        req.query.clear();
    }

    std::string_view version_part = line.substr(second_sp + 1);
    if (version_part != "HTTP/1.1" && version_part != "HTTP/1.0") {
        error_status_ = StatusCode::BadRequest;
        error_message_ = "Unsupported HTTP version: " + std::string(version_part);
        return false;
    }
    req.version = std::string(version_part);

    return true;
}

bool HttpParser::parse_headers(std::string_view header_block, HeaderMap& headers, bool is_response) {
    size_t pos = 0;
    size_t header_count = 0;

    while (pos < header_block.size()) {
        size_t next_line = header_block.find("\r\n", pos);
        size_t delim_len = 2;
        if (next_line == std::string_view::npos) {
            next_line = header_block.find('\n', pos);
            delim_len = 1;
        }

        std::string_view raw_line = (next_line == std::string_view::npos)
                                        ? header_block.substr(pos)
                                        : header_block.substr(pos, next_line - pos);

        pos = (next_line == std::string_view::npos) ? header_block.size() : next_line + delim_len;

        std::string_view line = trim_crlf(raw_line);
        if (line.empty()) continue;

        header_count++;
        if (header_count > max_header_count_) {
            error_status_ = is_response ? StatusCode::BadGateway : StatusCode::RequestHeaderFieldsTooLarge;
            error_message_ = "Exceeded maximum allowed number of headers.";
            return false;
        }

        size_t colon_pos = line.find(':');
        if (colon_pos == std::string_view::npos) {
            error_status_ = is_response ? StatusCode::BadGateway : StatusCode::BadRequest;
            error_message_ = "Malformed header line: missing colon delimiter.";
            return false;
        }

        std::string_view key = line.substr(0, colon_pos);
        std::string_view val = trim_whitespace(line.substr(colon_pos + 1));

        // RFC 9110: Header field name must not have whitespace before colon and must be valid token
        if (key.empty() || !is_valid_token(key)) {
            error_status_ = is_response ? StatusCode::BadGateway : StatusCode::BadRequest;
            error_message_ = "Invalid header field name: must conform to token grammar.";
            return false;
        }

        // Check for CRLF injection in header value
        if (contains_crlf(val)) {
            error_status_ = is_response ? StatusCode::BadGateway : StatusCode::BadRequest;
            error_message_ = "Header value contains illegal CR or LF character.";
            return false;
        }

        std::string key_str(key);
        std::string val_str(val);

        // Security rule: Reject all duplicate Content-Length headers to prevent smuggling
        auto it = headers.find(key_str);
        if (it != headers.end()) {
            std::string lower_key = key_str;
            std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (lower_key == "content-length") {
                error_status_ = is_response ? StatusCode::BadGateway : StatusCode::BadRequest;
                error_message_ = "Duplicate Content-Length header detected.";
                return false;
            }

            if (lower_key == "host") {
                error_status_ = StatusCode::BadRequest;
                error_message_ = "Multiple Host headers detected.";
                return false;
            }

            if (lower_key == "transfer-encoding") {
                error_status_ = StatusCode::BadRequest;
                error_message_ = "Duplicate Transfer-Encoding headers detected.";
                return false;
            }

            // For other headers: append comma-separated values (RFC 9110 Section 5.3)
            it->second += ", " + val_str;
        } else {
            headers[key_str] = val_str;
        }
    }

    return true;
}

ParseResult HttpParser::parse_response(
    std::string_view data,
    HttpResponse& out_response,
    size_t& bytes_consumed,
    bool is_eof,
    bool is_head_request
) {
    bytes_consumed = 0;

    size_t header_end = data.find("\r\n\r\n");
    size_t delimiter_len = 4;
    if (header_end == std::string_view::npos) {
        header_end = data.find("\n\n");
        delimiter_len = 2;
    }

    if (header_end == std::string_view::npos) {
        if (data.size() > max_header_size_) {
            error_status_ = StatusCode::BadGateway;
            error_message_ = "Backend response headers exceeded maximum allowed size.";
            return ParseResult::Error;
        }
        return ParseResult::Incomplete;
    }

    if (header_end > max_header_size_) {
        error_status_ = StatusCode::BadGateway;
        error_message_ = "Backend response headers exceeded maximum allowed size.";
        return ParseResult::Error;
    }

    std::string_view header_section = data.substr(0, header_end);
    size_t line_end = header_section.find("\r\n");
    size_t line_delim = 2;
    if (line_end == std::string_view::npos) {
        line_end = header_section.find('\n');
        line_delim = 1;
    }

    if (line_end == std::string_view::npos) {
        error_status_ = StatusCode::BadGateway;
        error_message_ = "Malformed backend response: missing status line delimiter.";
        return ParseResult::Error;
    }

    std::string_view status_line = header_section.substr(0, line_end);
    if (!parse_status_line(status_line, out_response)) {
        return ParseResult::Error;
    }

    std::string_view headers_block = header_section.substr(line_end + line_delim);
    out_response.headers.clear();
    if (!parse_headers(headers_block, out_response.headers, true)) {
        return ParseResult::Error;
    }

    int code = static_cast<int>(out_response.status_code);

    // RFC 9112 Section 6.3: Responses that MUST NOT contain a message body:
    // - Responses to HEAD requests
    // - 1xx Informational
    // - 204 No Content
    // - 304 Not Modified
    if (is_head_request || (code >= 100 && code < 200) || code == 204 || code == 304) {
        out_response.body.clear();
        bytes_consumed = header_end + delimiter_len;
        return ParseResult::Complete;
    }

    // Check for conflicting framing in backend response
    bool has_te = out_response.has_header("Transfer-Encoding");
    bool has_cl = out_response.has_header("Content-Length");
    if (has_te && has_cl) {
        error_status_ = StatusCode::BadGateway;
        error_message_ = "Conflicting framing headers in backend response (Transfer-Encoding and Content-Length).";
        return ParseResult::Error;
    }

    size_t content_length = 0;
    if (has_cl) {
        auto cl_str = out_response.get_header("Content-Length").value_or("");
        cl_str = std::string(trim_whitespace(cl_str));

        if (cl_str.empty()) {
            error_status_ = StatusCode::BadGateway;
            error_message_ = "Empty Content-Length in backend response.";
            return ParseResult::Error;
        }

        size_t parsed_len = 0;
        auto [ptr, ec] = std::from_chars(cl_str.data(), cl_str.data() + cl_str.size(), parsed_len);
        if (ec != std::errc{} || ptr != cl_str.data() + cl_str.size()) {
            error_status_ = StatusCode::BadGateway;
            error_message_ = "Invalid Content-Length in backend response.";
            return ParseResult::Error;
        }

        if (parsed_len > max_body_size_) {
            error_status_ = StatusCode::BadGateway;
            error_message_ = "Backend response body exceeds maximum configured limit.";
            return ParseResult::Error;
        }

        content_length = parsed_len;

        // Safe integer arithmetic check
        if (content_length > std::numeric_limits<size_t>::max() - (header_end + delimiter_len)) {
            error_status_ = StatusCode::BadGateway;
            error_message_ = "Backend response Content-Length overflow.";
            return ParseResult::Error;
        }

        size_t total_required = header_end + delimiter_len + content_length;
        if (data.size() < total_required) {
            return ParseResult::Incomplete;
        }

        if (content_length > 0) {
            out_response.body = std::string(data.substr(header_end + delimiter_len, content_length));
        } else {
            out_response.body.clear();
        }

        bytes_consumed = total_required;
        return ParseResult::Complete;
    }

    // Close-delimited response: Wait for EOF
    if (!is_eof) {
        return ParseResult::Incomplete;
    }

    size_t body_size = data.size() - (header_end + delimiter_len);
    if (body_size > max_body_size_) {
        error_status_ = StatusCode::BadGateway;
        error_message_ = "Close-delimited backend response exceeds maximum configured body size.";
        return ParseResult::Error;
    }

    out_response.body = std::string(data.substr(header_end + delimiter_len));
    bytes_consumed = data.size();
    return ParseResult::Complete;
}

bool HttpParser::parse_status_line(std::string_view line, HttpResponse& resp) {
    line = trim_crlf(line);
    // Expected: VERSION SP STATUS_CODE SP REASON
    size_t first_sp = line.find(' ');
    if (first_sp == std::string_view::npos) {
        error_status_ = StatusCode::BadGateway;
        error_message_ = "Malformed response status line: missing space after version.";
        return false;
    }

    std::string_view version_part = line.substr(0, first_sp);
    if (version_part != "HTTP/1.1" && version_part != "HTTP/1.0") {
        error_status_ = StatusCode::BadGateway;
        error_message_ = "Unsupported backend HTTP response version: " + std::string(version_part);
        return false;
    }
    resp.version = std::string(version_part);

    size_t second_sp = line.find(' ', first_sp + 1);
    std::string_view code_part;
    if (second_sp == std::string_view::npos) {
        code_part = line.substr(first_sp + 1);
        resp.reason_phrase.clear();
    } else {
        code_part = line.substr(first_sp + 1, second_sp - (first_sp + 1));
        resp.reason_phrase = std::string(trim_whitespace(line.substr(second_sp + 1)));
    }

    if (code_part.size() != 3) {
        error_status_ = StatusCode::BadGateway;
        error_message_ = "Status code must be exactly 3 digits.";
        return false;
    }

    int code = 0;
    auto [ptr, ec] = std::from_chars(code_part.data(), code_part.data() + code_part.size(), code);
    if (ec != std::errc{} || ptr != code_part.data() + code_part.size() || code < 100 || code > 599) {
        error_status_ = StatusCode::BadGateway;
        error_message_ = "Invalid numeric HTTP status code.";
        return false;
    }

    resp.status_code = static_cast<StatusCode>(code);
    return true;
}

} // namespace http
