# HTTP Error Codes & Failure Modes

| Status Code | Reason Phrase | Trigger Condition |
| :--- | :--- | :--- |
| **`400`** | `Bad Request` | Malformed request line, missing colon delimiter in headers, conflicting `Content-Length` headers. |
| **`405`** | `Method Not Allowed` | Unrecognized or unsupported HTTP method (e.g. `TRACE`). |
| **`408`** | `Request Timeout` | Client connection inactive beyond `connection_timeout_ms`. |
| **`413`** | `Payload Too Large` | Request body exceeds configured `max_body_size`. |
| **`431`** | `Request Header Fields Too Large` | Request headers exceed configured `max_header_size`. |
| **`501`** | `Not Implemented` | Unsupported transfer encoding framing (e.g. `chunked`). |
| **`502`** | `Bad Gateway` | Upstream backend connection refused, reset, or returned malformed HTTP framing. |
| **`503`** | `Service Unavailable` | All backend instances are marked unhealthy, or `ThreadPool` queue is saturated (with `Retry-After: 5`). |
| **`504`** | `Gateway Timeout` | Upstream backend failed to return a response within `backend_timeout_ms`. |
