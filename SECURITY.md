# Security Policy

## 1. Supported Versions
| Version | Supported |
| :--- | :--- |
| `1.0.x` | Yes |

## 2. Threat Mitigations Implemented
- **Memory Safety:** Move-only RAII abstractions eliminate double-free, use-after-free, and dangling descriptor vulnerabilities. Validated under AddressSanitizer and UndefinedBehaviorSanitizer.
- **Request Smuggling Prevention:** Conflicting `Content-Length` headers and unsupported `Transfer-Encoding: chunked` requests are rejected with strict errors.
- **DoS / Resource Exhaustion Protection:** Bounded task queues reject excess traffic under load. Configurable maximum limits on header size (8 KB) and request body (10 MB).
- **Socket Timeouts:** Connection receive/send timeouts eliminate Slowloris attack vectors.

## 3. Reporting Vulnerabilities
To report a security issue, please open a security advisory or email the maintainer directly.
