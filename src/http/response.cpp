#include "http/response.h"
#include <sstream>
#include <algorithm>
#include <charconv>

namespace http {

HttpResponse::HttpResponse(StatusCode code)
    : status_code(code), reason_phrase(status_code_to_reason(code)) {}

bool HttpResponse::has_header(const std::string& key) const noexcept {
    return headers.find(key) != headers.end();
}

std::optional<std::string> HttpResponse::get_header(const std::string& key) const noexcept {
    auto it = headers.find(key);
    if (it != headers.end()) {
        return it->second;
    }
    return std::nullopt;
}

void HttpResponse::set_header(std::string key, std::string value) {
    headers[std::move(key)] = std::move(value);
}

void HttpResponse::remove_header(const std::string& key) {
    headers.erase(key);
}

size_t HttpResponse::content_length() const noexcept {
    auto cl = get_header("Content-Length");
    if (cl) {
        size_t val = 0;
        auto [ptr, ec] = std::from_chars(cl->data(), cl->data() + cl->size(), val);
        if (ec == std::errc{} && ptr == cl->data() + cl->size()) {
            return val;
        }
    }
    return body.size();
}

bool HttpResponse::is_keep_alive() const noexcept {
    auto conn = get_header("Connection");
    if (conn) {
        std::string val = *conn;
        std::transform(val.begin(), val.end(), val.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (val.find("close") != std::string::npos) return false;
        if (val.find("keep-alive") != std::string::npos) return true;
    }
    return version == "HTTP/1.1";
}

std::string HttpResponse::to_string() const {
    std::ostringstream oss;
    oss << version << " " << static_cast<int>(status_code) << " " << reason_phrase << "\r\n";

    bool has_content_length = false;
    for (const auto& [k, v] : headers) {
        std::string lower = k;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower == "content-length") {
            has_content_length = true;
        }
        oss << k << ": " << v << "\r\n";
    }

    if (!has_content_length) {
        oss << "Content-Length: " << body.size() << "\r\n";
    }

    oss << "\r\n";
    if (!body.empty()) {
        oss << body;
    }
    return oss.str();
}

HttpResponse HttpResponse::make_json(StatusCode code, const std::string& json_body) {
    HttpResponse resp(code);
    resp.set_header("Content-Type", "application/json; charset=utf-8");
    resp.set_header("Content-Length", std::to_string(json_body.size()));
    resp.set_header("Server", "cpp-load-balancer/1.0");
    resp.body = json_body;
    return resp;
}

HttpResponse HttpResponse::make_text(StatusCode code, const std::string& text_body) {
    HttpResponse resp(code);
    resp.set_header("Content-Type", "text/plain; charset=utf-8");
    resp.set_header("Content-Length", std::to_string(text_body.size()));
    resp.set_header("Server", "cpp-load-balancer/1.0");
    resp.body = text_body;
    return resp;
}

HttpResponse HttpResponse::make_html(StatusCode code, const std::string& html_body) {
    HttpResponse resp(code);
    resp.set_header("Content-Type", "text/html; charset=utf-8");
    resp.set_header("Content-Length", std::to_string(html_body.size()));
    resp.set_header("Server", "cpp-load-balancer/1.0");
    resp.body = html_body;
    return resp;
}

HttpResponse HttpResponse::make_error(StatusCode code, const std::string& message) {
    std::string msg = message.empty() ? status_code_to_reason(code) : message;
    std::string json = "{\"error\":\"" + msg + "\",\"status\":" + std::to_string(static_cast<int>(code)) + "}";
    HttpResponse resp = make_json(code, json);
    resp.set_header("Connection", "close");
    return resp;
}

} // namespace http
