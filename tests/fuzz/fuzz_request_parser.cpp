#include "http/http_parser.h"
#include <cstdint>
#include <cstddef>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *Data, size_t Size) {
    if (Size == 0 || Size > 65536) {
        return 0; // Cap input size to prevent memory exhaustion in fuzzer
    }

    http::HttpParser parser(8192, 1024 * 1024);
    http::HttpRequest req;
    size_t consumed = 0;

    std::string_view input(reinterpret_cast<const char*>(Data), Size);
    parser.parse_request(input, req, consumed);

    return 0;
}
