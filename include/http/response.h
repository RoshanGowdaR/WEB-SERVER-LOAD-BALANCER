#pragma once

#include "http/http_types.h"
#include "http/request.h" // For HeaderMap & CaseInsensitiveCompare
#include <string>
#include <optional>

namespace http {

class HttpResponse {
public:
    HttpResponse() = default;
    explicit HttpResponse(StatusCode code);

    StatusCode status_code{StatusCode::OK};
    std::string reason_phrase{"OK"};
    std::string version{"HTTP/1.1"};
    HeaderMap headers;
    std::string body;

    [[nodiscard]] bool has_header(const std::string& key) const noexcept;
    [[nodiscard]] std::optional<std::string> get_header(const std::string& key) const noexcept;
    void set_header(std::string key, std::string value);
    void remove_header(const std::string& key);

    [[nodiscard]] size_t content_length() const noexcept;
    [[nodiscard]] bool is_keep_alive() const noexcept;

    [[nodiscard]] std::string to_string() const;

    // Factory methods
    static HttpResponse make_json(StatusCode code, const std::string& json_body);
    static HttpResponse make_text(StatusCode code, const std::string& text_body);
    static HttpResponse make_html(StatusCode code, const std::string& html_body);
    static HttpResponse make_error(StatusCode code, const std::string& message = "");
};

} // namespace http
