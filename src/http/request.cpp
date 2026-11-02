#include "http/request.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <charconv>

namespace http {

bool CaseInsensitiveCompare::operator()(const std::string& a, const std::string& b) const noexcept {
    return std::lexicographical_compare(
        a.begin(), a.end(), b.begin(), b.end(),
        [](char c1, char c2) {
            return std::tolower(static_cast<unsigned char>(c1)) <
                   std::tolower(static_cast<unsigned char>(c2));
        });
}

bool HttpRequest::has_header(const std::string& key) const noexcept {
    return headers.find(key) != headers.end();
}

std::optional<std::string> HttpRequest::get_header(const std::string& key) const noexcept {
    auto it = headers.find(key);
    if (it != headers.end()) {
        return it->second;
    }
    return std::nullopt;
}

void HttpRequest::set_header(std::string key, std::string value) {
    headers[std::move(key)] = std::move(value);
}

void HttpRequest::append_header_value(const std::string& key, const std::string& value) {
    auto it = headers.find(key);
    if (it != headers.end()) {
        it->second += ", " + value;
    } else {
        headers[key] = value;
    }
}

size_t HttpRequest::content_length() const noexcept {
    auto cl = get_header("Content-Length");
    if (cl) {
        size_t val = 0;
        auto [ptr, ec] = std::from_chars(cl->data(), cl->data() + cl->size(), val);
        if (ec == std::errc{} && ptr == cl->data() + cl->size()) {
            return val;
        }
    }
    return 0;
}

bool HttpRequest::is_keep_alive() const noexcept {
    auto conn = get_header("Connection");
    if (conn) {
        std::string val = *conn;
        std::transform(val.begin(), val.end(), val.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (val.find("close") != std::string::npos) return false;
        if (val.find("keep-alive") != std::string::npos) return true;
    }
    // HTTP/1.1 defaults to keep-alive; HTTP/1.0 defaults to close
    return version == "HTTP/1.1";
}

std::string HttpRequest::to_string() const {
    std::ostringstream oss;
    oss << method_to_string(method) << " " << (uri.empty() ? "/" : uri) << " " << version << "\r\n";
    for (const auto& [k, v] : headers) {
        oss << k << ": " << v << "\r\n";
    }
    oss << "\r\n";
    if (!body.empty()) {
        oss << body;
    }
    return oss.str();
}

} // namespace http
