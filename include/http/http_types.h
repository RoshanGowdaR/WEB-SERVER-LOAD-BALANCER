#pragma once

#include <string>
#include <string_view>

namespace http {

enum class Method {
    GET,
    POST,
    PUT,
    DELETE,
    HEAD,
    OPTIONS,
    PATCH,
    UNKNOWN
};

enum class StatusCode {
    OK = 200,
    Created = 201,
    Accepted = 202,
    NoContent = 204,
    MovedPermanently = 301,
    Found = 302,
    NotModified = 304,
    BadRequest = 400,
    Unauthorized = 401,
    Forbidden = 403,
    NotFound = 404,
    MethodNotAllowed = 405,
    RequestTimeout = 408,
    Conflict = 409,
    LengthRequired = 411,
    PayloadTooLarge = 413,
    UriTooLong = 414,
    RequestHeaderFieldsTooLarge = 431,
    InternalServerError = 500,
    NotImplemented = 501,
    BadGateway = 502,
    ServiceUnavailable = 503,
    GatewayTimeout = 504
};

std::string method_to_string(Method method);
Method string_to_method(std::string_view method_str);
std::string status_code_to_reason(StatusCode code);

} // namespace http
