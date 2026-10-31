#include "http/http_parser.h"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace http {

namespace {

std::string_view trim(std::string_view str) {
    while (!str.empty() && (str.front() == ' ' || str.front() == '\t')) {
        str.remove_prefix(1);
    }
    while (!str.empty() && (str.back() == ' ' || str.back() == '\t' || str.back() == '\r' || str.back() == '\n')) {
        str.remove_suffix(1);
    }
    return str;
}

} // namespace

HttpParser::HttpParser(size_t max_header_size, size_t max_body_size)
    : max_header_size_(max_header_size),
      max_body_size_(max_body_size) {}

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
        error_message_ = "Malformed request: missing request line.";
        return ParseResult::Error;
    }

    std::string_view request_line = header_section.substr(0, line_end);
    if (!parse_request_line(request_line, out_request)) {
        return ParseResult::Error;
    }

    std::string_view headers_block = header_section.substr(line_end + line_delim);
    if (!parse_headers(headers_block, out_request.headers)) {
        return ParseResult::Error;
    }

    // Transfer-Encoding check: We currently do not support chunked transfer encoding downstream.
    if (out_request.has_header("Transfer-Encoding")) {
        auto te = out_request.get_header("Transfer-Encoding").value_or("");
        if (!te.empty() && te != "identity") {
            error_status_ = StatusCode::NotImplemented;
            error_message_ = "Transfer-Encoding is not supported; use Content-Length.";
            return ParseResult::Error;
        }
    }

    size_t content_length = 0;
    if (out_request.has_header("Content-Length")) {
        auto cl_str = out_request.get_header("Content-Length").value_or("0");
        try {
            content_length = std::stoul(cl_str);
        } catch (...) {
            error_status_ = StatusCode::BadRequest;
            error_message_ = "Invalid Content-Length header value.";
            return ParseResult::Error;
        }

        if (content_length > max_body_size_) {
            error_status_ = StatusCode::PayloadTooLarge;
            error_message_ = "Request payload exceeds configured limit.";
            return ParseResult::Error;
        }
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
    // Expected: METHOD SP URI SP VERSION
    size_t first_sp = line.find(' ');
    if (first_sp == std::string_view::npos) {
        error_status_ = StatusCode::BadRequest;
        error_message_ = "Malformed request line: missing space after method.";
        return false;
    }

    std::string_view method_part = line.substr(0, first_sp);
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

    std::string_view uri_part = line.substr(first_sp + 1, second_sp - (first_sp + 1));
    if (uri_part.empty() || uri_part[0] != '/') {
        error_status_ = StatusCode::BadRequest;
        error_message_ = "Invalid request URI: must start with '/'.";
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
    version_part = trim(version_part);
    if (version_part != "HTTP/1.1" && version_part != "HTTP/1.0") {
        error_status_ = StatusCode::BadRequest;
        error_message_ = "Unsupported HTTP version: " + std::string(version_part);
        return false;
    }
    req.version = std::string(version_part);

    return true;
}

bool HttpParser::parse_headers(std::string_view header_block, HeaderMap& headers) {
    size_t pos = 0;
    while (pos < header_block.size()) {
        size_t next_line = header_block.find("\r\n", pos);
        size_t delim_len = 2;
        if (next_line == std::string_view::npos) {
            next_line = header_block.find('\n', pos);
            delim_len = 1;
        }

        std::string_view line = (next_line == std::string_view::npos)
                                    ? header_block.substr(pos)
                                    : header_block.substr(pos, next_line - pos);

        pos = (next_line == std::string_view::npos) ? header_block.size() : next_line + delim_len;

        line = trim(line);
        if (line.empty()) continue;

        size_t colon_pos = line.find(':');
        if (colon_pos == std::string_view::npos) {
            error_status_ = StatusCode::BadRequest;
            error_message_ = "Malformed header line: missing colon delimiter.";
            return false;
        }

        std::string_view key = trim(line.substr(0, colon_pos));
        std::string_view val = trim(line.substr(colon_pos + 1));

        if (key.empty()) {
            error_status_ = StatusCode::BadRequest;
            error_message_ = "Malformed header line: empty header field name.";
            return false;
        }

        // Duplicate Content-Length check to guard against request smuggling
        std::string key_str(key);
        if (key_str == "Content-Length" || key_str == "content-length") {
            auto it = headers.find(key_str);
            if (it != headers.end() && it->second != val) {
                error_status_ = StatusCode::BadRequest;
                error_message_ = "Conflicting Content-Length headers detected.";
                return false;
            }
        }

        headers[key_str] = std::string(val);
    }
    return true;
}

ParseResult HttpParser::parse_response(std::string_view data, HttpResponse& out_response, size_t& bytes_consumed, bool is_eof) {
    bytes_consumed = 0;

    size_t header_end = data.find("\r\n\r\n");
    size_t delimiter_len = 4;
    if (header_end == std::string_view::npos) {
        header_end = data.find("\n\n");
        delimiter_len = 2;
    }

    if (header_end == std::string_view::npos) {
        return ParseResult::Incomplete;
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
        error_message_ = "Malformed backend response: missing status line.";
        return ParseResult::Error;
    }

    std::string_view status_line = header_section.substr(0, line_end);
    if (!parse_status_line(status_line, out_response)) {
        return ParseResult::Error;
    }

    std::string_view headers_block = header_section.substr(line_end + line_delim);
    if (!parse_headers(headers_block, out_response.headers)) {
        return ParseResult::Error;
    }

    size_t content_length = 0;
    bool has_cl = false;
    if (out_response.has_header("Content-Length")) {
        has_cl = true;
        auto cl_str = out_response.get_header("Content-Length").value_or("0");
        try {
            content_length = std::stoul(cl_str);
        } catch (...) {
            error_status_ = StatusCode::BadGateway;
            error_message_ = "Invalid Content-Length in backend response.";
            return ParseResult::Error;
        }
    }

    if (has_cl) {
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

    // If no Content-Length, wait for EOF (connection close) before completing
    if (!is_eof) {
        return ParseResult::Incomplete;
    }

    out_response.body = std::string(data.substr(header_end + delimiter_len));
    bytes_consumed = data.size();
    return ParseResult::Complete;
}

bool HttpParser::parse_status_line(std::string_view line, HttpResponse& resp) {
    // Expected: VERSION SP STATUS_CODE SP REASON
    size_t first_sp = line.find(' ');
    if (first_sp == std::string_view::npos) {
        error_status_ = StatusCode::BadGateway;
        error_message_ = "Malformed response status line.";
        return false;
    }

    resp.version = std::string(line.substr(0, first_sp));

    size_t second_sp = line.find(' ', first_sp + 1);
    std::string_view code_part;
    if (second_sp == std::string_view::npos) {
        code_part = line.substr(first_sp + 1);
        resp.reason_phrase = "";
    } else {
        code_part = line.substr(first_sp + 1, second_sp - (first_sp + 1));
        resp.reason_phrase = std::string(trim(line.substr(second_sp + 1)));
    }

    try {
        int code = std::stoi(std::string(code_part));
        resp.status_code = static_cast<StatusCode>(code);
    } catch (...) {
        error_status_ = StatusCode::BadGateway;
        error_message_ = "Invalid status code in response.";
        return false;
    }

    return true;
}

} // namespace http
