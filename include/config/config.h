#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <chrono>

namespace config {

struct BackendConfig {
    std::string host{"127.0.0.1"};
    uint16_t port{8081};
    int weight{1};
};

struct ServerConfig {
    std::string host{"127.0.0.1"};
    uint16_t port{8080};
    size_t worker_threads{4};
    size_t max_queue_size{1024};
    std::chrono::milliseconds connection_timeout{5000};
    std::chrono::milliseconds backend_timeout{5000};
    std::chrono::milliseconds health_check_interval{5000};
    size_t max_header_size{8192};
    size_t max_body_size{10 * 1024 * 1024};
    std::string algorithm{"round_robin"}; // "round_robin" or "least_connections"
    std::vector<BackendConfig> backends;

    static ServerConfig load_from_file(const std::string& filepath);
    static ServerConfig default_config();

    [[nodiscard]] bool validate(std::string& error_message) const noexcept;
    [[nodiscard]] std::string to_json_string() const;
};

} // namespace config
