# HTTP Protocol Support Specification

## 1. Supported Specifications
The HTTP parser adheres to the RFC 9112 (HTTP/1.1) and RFC 9110 (HTTP Semantics) specifications for request framing and parsing.

| Feature | Supported | Behavior / Handling |
| :--- | :--- | :--- |
| **HTTP Methods** | `GET`, `POST`, `PUT`, `DELETE`, `HEAD`, `OPTIONS` | Validated; unknown verbs return `405 Method Not Allowed`. |
| **HTTP Versions** | `HTTP/1.1`, `HTTP/1.0` | Parsed in request-line; others return `400 Bad Request`. |
| **Header Framing** | Standard CRLF (`\r\n`) or LF (`\n`) | Lines parsed into case-insensitive header map. |
| **Content-Length** | Yes | Strict integer validation; body read up to specified length. |
| **Chunked Transfer** | Downstream rejected | `Transfer-Encoding: chunked` requests return `501 Not Implemented` to prevent HTTP request smuggling. |
| **Keep-Alive** | Yes | `Connection: keep-alive` and `Connection: close` honored. Default for HTTP/1.1 is keep-alive. |
| **Forwarded Headers**| Injected | Adds `X-Forwarded-For: <client_ip>`, `X-Forwarded-Proto: http`, `Via: 1.1 cpp-load-balancer`. |
