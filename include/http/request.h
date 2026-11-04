#pragma once

#include "http/http_types.h"
#include <string>
#include <map>
#include <optional>
#include <cstdint>

namespace http {

// Case-insensitive comparator for HTTP headers
struct CaseInsensitiveCompare {
    bool operator()(const std::string& a, const std::string& b) const noexcept;
};

using HeaderMap = std::map<std::string, std::string, CaseInsensitiveCompare>;

class HttpRequest {
public:
    HttpRequest() = default;

    Method method{Method::UNKNOWN};
    std::string uri;
    std::string path;
    std::string query;
    std::string version{"HTTP/1.1"};
    HeaderMap headers;
    std::string body;

    [[nodiscard]] bool has_header(const std::string& key) const noexcept;
    [[nodiscard]] std::optional<std::string> get_header(const std::string& key) const noexcept;
    void set_header(std::string key, std::string value);
    void append_header_value(const std::string& key, const std::string& value);

    [[nodiscard]] size_t content_length() const noexcept;
    [[nodiscard]] bool is_keep_alive() const noexcept;
    [[nodiscard]] bool expects_100_continue() const noexcept;
    void remove_header(const std::string& key);

    [[nodiscard]] std::string to_string() const;
};

} // namespace http
