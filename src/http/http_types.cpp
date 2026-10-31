#include "http/http_types.h"
#include <algorithm>

namespace http {

std::string method_to_string(Method method) {
    switch (method) {
        case Method::GET: return "GET";
        case Method::POST: return "POST";
        case Method::PUT: return "PUT";
        case Method::DELETE: return "DELETE";
        case Method::HEAD: return "HEAD";
        case Method::OPTIONS: return "OPTIONS";
        case Method::PATCH: return "PATCH";
        case Method::UNKNOWN: default: return "UNKNOWN";
    }
}

Method string_to_method(std::string_view method_str) {
    if (method_str == "GET") return Method::GET;
    if (method_str == "POST") return Method::POST;
    if (method_str == "PUT") return Method::PUT;
    if (method_str == "DELETE") return Method::DELETE;
    if (method_str == "HEAD") return Method::HEAD;
    if (method_str == "OPTIONS") return Method::OPTIONS;
    if (method_str == "PATCH") return Method::PATCH;
    return Method::UNKNOWN;
}

std::string status_code_to_reason(StatusCode code) {
    switch (code) {
        case StatusCode::OK: return "OK";
        case StatusCode::Created: return "Created";
        case StatusCode::Accepted: return "Accepted";
        case StatusCode::NoContent: return "No Content";
        case StatusCode::MovedPermanently: return "Moved Permanently";
        case StatusCode::Found: return "Found";
        case StatusCode::NotModified: return "Not Modified";
        case StatusCode::BadRequest: return "Bad Request";
        case StatusCode::Unauthorized: return "Unauthorized";
        case StatusCode::Forbidden: return "Forbidden";
        case StatusCode::NotFound: return "Not Found";
        case StatusCode::MethodNotAllowed: return "Method Not Allowed";
        case StatusCode::RequestTimeout: return "Request Timeout";
        case StatusCode::Conflict: return "Conflict";
        case StatusCode::LengthRequired: return "Length Required";
        case StatusCode::PayloadTooLarge: return "Payload Too Large";
        case StatusCode::UriTooLong: return "URI Too Long";
        case StatusCode::RequestHeaderFieldsTooLarge: return "Request Header Fields Too Large";
        case StatusCode::InternalServerError: return "Internal Server Error";
        case StatusCode::NotImplemented: return "Not Implemented";
        case StatusCode::BadGateway: return "Bad Gateway";
        case StatusCode::ServiceUnavailable: return "Service Unavailable";
        case StatusCode::GatewayTimeout: return "Gateway Timeout";
        default: return "Unknown";
    }
}

} // namespace http
