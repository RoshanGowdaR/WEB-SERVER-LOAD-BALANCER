#include "config/config.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <regex>

namespace config {

namespace {

// Simple key-value string extractor for JSON
std::string extract_json_string(const std::string& json, const std::string& key, const std::string& default_val = "") {
    std::regex re("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch match;
    if (std::regex_search(json, match, re) && match.size() > 1) {
        return match.str(1);
    }
    return default_val;
}

long long extract_json_int(const std::string& json, const std::string& key, long long default_val = 0) {
    std::regex re("\"" + key + "\"\\s*:\\s*([0-9]+)");
    std::smatch match;
    if (std::regex_search(json, match, re) && match.size() > 1) {
        try {
            return std::stoll(match.str(1));
        } catch (...) {
            return default_val;
        }
    }
    return default_val;
}

} // namespace

ServerConfig ServerConfig::default_config() {
    ServerConfig cfg;
    cfg.host = "127.0.0.1";
    cfg.port = 8080;
    cfg.worker_threads = 4;
    cfg.max_queue_size = 1024;
    cfg.connection_timeout = std::chrono::milliseconds(5000);
    cfg.backend_timeout = std::chrono::milliseconds(5000);
    cfg.health_check_interval = std::chrono::milliseconds(5000);
    cfg.max_header_size = 8192;
    cfg.max_body_size = 10 * 1024 * 1024;
    cfg.algorithm = "round_robin";

    BackendConfig b1{"127.0.0.1", 9001, 1};
    BackendConfig b2{"127.0.0.1", 9002, 1};
    BackendConfig b3{"127.0.0.1", 9003, 1};
    cfg.backends = {b1, b2, b3};
    return cfg;
}

ServerConfig ServerConfig::load_from_file(const std::string& filepath) {
    ServerConfig cfg = default_config();

    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open configuration file: " + filepath);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string json = buffer.str();

    std::string host_val = extract_json_string(json, "host");
    if (!host_val.empty()) cfg.host = host_val;

    long long port_val = extract_json_int(json, "port", -1);
    if (port_val > 0) cfg.port = static_cast<uint16_t>(port_val);

    long long workers_val = extract_json_int(json, "worker_threads", -1);
    if (workers_val > 0) cfg.worker_threads = static_cast<size_t>(workers_val);

    long long queue_val = extract_json_int(json, "max_queue_size", -1);
    if (queue_val > 0) cfg.max_queue_size = static_cast<size_t>(queue_val);

    long long conn_to = extract_json_int(json, "connection_timeout_ms", -1);
    if (conn_to > 0) cfg.connection_timeout = std::chrono::milliseconds(conn_to);

    long long backend_to = extract_json_int(json, "backend_timeout_ms", -1);
    if (backend_to > 0) cfg.backend_timeout = std::chrono::milliseconds(backend_to);

    long long health_int = extract_json_int(json, "health_check_interval_ms", -1);
    if (health_int > 0) cfg.health_check_interval = std::chrono::milliseconds(health_int);

    long long max_hdr = extract_json_int(json, "max_header_size", -1);
    if (max_hdr > 0) cfg.max_header_size = static_cast<size_t>(max_hdr);

    long long max_body = extract_json_int(json, "max_body_size", -1);
    if (max_body > 0) cfg.max_body_size = static_cast<size_t>(max_body);

    std::string algo = extract_json_string(json, "algorithm");
    if (!algo.empty()) cfg.algorithm = algo;

    // Parse backends array
    size_t backends_pos = json.find("\"backends\"");
    if (backends_pos != std::string::npos) {
        size_t array_start = json.find('[', backends_pos);
        size_t array_end = json.find(']', array_start);
        if (array_start != std::string::npos && array_end != std::string::npos) {
            std::string backends_json = json.substr(array_start, array_end - array_start + 1);
            std::vector<BackendConfig> parsed_backends;

            std::regex obj_re("\\{[^\\}]*\\}");
            auto begin = std::sregex_iterator(backends_json.begin(), backends_json.end(), obj_re);
            auto end = std::sregex_iterator();

            for (std::sregex_iterator i = begin; i != end; ++i) {
                std::string obj_str = i->str();
                BackendConfig b;
                b.host = extract_json_string(obj_str, "host", "127.0.0.1");
                b.port = static_cast<uint16_t>(extract_json_int(obj_str, "port", 8080));
                b.weight = static_cast<int>(extract_json_int(obj_str, "weight", 1));
                parsed_backends.push_back(b);
            }

            if (!parsed_backends.empty()) {
                cfg.backends = std::move(parsed_backends);
            }
        }
    }

    return cfg;
}

bool ServerConfig::validate(std::string& error_message) const noexcept {
    if (port == 0) {
        error_message = "Listen port must be greater than 0.";
        return false;
    }
    if (worker_threads == 0) {
        error_message = "worker_threads must be at least 1.";
        return false;
    }
    if (max_queue_size == 0) {
        error_message = "max_queue_size must be at least 1.";
        return false;
    }
    if (backends.empty()) {
        error_message = "At least one backend server must be configured.";
        return false;
    }
    for (const auto& b : backends) {
        if (b.host.empty() || b.port == 0) {
            error_message = "Invalid backend configuration: host cannot be empty, port must be > 0.";
            return false;
        }
    }
    if (algorithm != "round_robin" && algorithm != "least_connections") {
        error_message = "Unsupported algorithm: '" + algorithm + "'. Must be 'round_robin' or 'least_connections'.";
        return false;
    }
    return true;
}

std::string ServerConfig::to_json_string() const {
    std::ostringstream ss;
    ss << "{\n"
       << "  \"host\": \"" << host << "\",\n"
       << "  \"port\": " << port << ",\n"
       << "  \"worker_threads\": " << worker_threads << ",\n"
       << "  \"max_queue_size\": " << max_queue_size << ",\n"
       << "  \"connection_timeout_ms\": " << connection_timeout.count() << ",\n"
       << "  \"backend_timeout_ms\": " << backend_timeout.count() << ",\n"
       << "  \"health_check_interval_ms\": " << health_check_interval.count() << ",\n"
       << "  \"max_header_size\": " << max_header_size << ",\n"
       << "  \"max_body_size\": " << max_body_size << ",\n"
       << "  \"algorithm\": \"" << algorithm << "\",\n"
       << "  \"backends\": [\n";

    for (size_t i = 0; i < backends.size(); ++i) {
        ss << "    {\"host\": \"" << backends[i].host << "\", \"port\": " << backends[i].port
           << ", \"weight\": " << backends[i].weight << "}"
           << (i + 1 < backends.size() ? ",\n" : "\n");
    }
    ss << "  ]\n}";
    return ss.str();
}

} // namespace config
