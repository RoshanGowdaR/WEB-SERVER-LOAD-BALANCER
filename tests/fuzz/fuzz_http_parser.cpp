#include "http/http_parser.h"
#include <iostream>
#include <string>
#include <vector>
#include <random>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0) return 0;

    std::string_view input(reinterpret_cast<const char*>(data), size);

    http::HttpParser parser(8192, 1024 * 1024);
    http::HttpRequest req;
    size_t consumed = 0;
    parser.parse_request(input, req, consumed);

    http::HttpResponse resp;
    parser.parse_response(input, resp, consumed);

    return 0;
}

// Standalone fuzz runner for environments without libFuzzer
int main(int /*argc*/, char** /*argv*/) {
    std::cout << "[FuzzTarget] Starting HTTP parser mutation fuzzer...\n";

    std::vector<std::string> seed_corpus = {
        "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n",
        "POST /submit HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5\r\n\r\nhello",
        "INVALID REQUEST WITHOUT CRLF",
        "GET /a HTTP/1.1\r\nContent-Length: -5\r\n\r\n",
        "GET / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n"
    };

    std::mt19937 rng(42);
    for (const auto& seed : seed_corpus) {
        LLVMFuzzerTestOneInput(reinterpret_cast<const uint8_t*>(seed.data()), seed.size());
    }

    // Run 10,000 random mutations
    std::uniform_int_distribution<int> char_dist(0, 255);
    std::uniform_int_distribution<size_t> len_dist(1, 1024);

    for (int iter = 0; iter < 10000; ++iter) {
        size_t len = len_dist(rng);
        std::vector<uint8_t> buf(len);
        for (size_t i = 0; i < len; ++i) {
            buf[i] = static_cast<uint8_t>(char_dist(rng));
        }
        LLVMFuzzerTestOneInput(buf.data(), buf.size());
    }

    std::cout << "[FuzzTarget] Completed 10,000 fuzz iterations with zero crashes or sanitizer faults.\n";
    return 0;
}
