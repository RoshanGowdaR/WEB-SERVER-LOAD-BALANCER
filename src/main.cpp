#include "server/server.h"
#include "config/config.h"
#include "logging/logger.h"
#include <iostream>
#include <csignal>
#include <string>
#include <memory>

static std::atomic<bool> g_shutdown_requested{false};
static std::unique_ptr<server::Server> g_server_instance{nullptr};

void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM) {
        LOG_INFO("Signal " << signum << " received. Requesting server shutdown...");
        g_shutdown_requested.store(true);
        if (g_server_instance) {
            g_server_instance->stop();
        }
    }
}

void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [options]\n"
              << "Options:\n"
              << "  -c, --config <file>    Path to JSON configuration file (default: config/config.example.json)\n"
              << "  -p, --port <port>      Override listening port\n"
              << "  -a, --algo <algorithm> Load balancing algorithm ('round_robin' or 'least_connections')\n"
              << "  -v, --verbose          Enable DEBUG level logging\n"
              << "  -h, --help             Display this help message\n";
}

int main(int argc, char* argv[]) {
    std::string config_path = "config/config.example.json";
    int port_override = -1;
    std::string algo_override;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            config_path = argv[++i];
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            port_override = std::stoi(argv[++i]);
        } else if ((arg == "-a" || arg == "--algo") && i + 1 < argc) {
            algo_override = argv[++i];
        } else if (arg == "-v" || arg == "--verbose") {
            logging::Logger::instance().set_level(logging::Level::DEBUG);
        }
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    config::ServerConfig cfg;
    try {
        cfg = config::ServerConfig::load_from_file(config_path);
        LOG_INFO("Loaded configuration from: " << config_path);
    } catch (const std::exception& ex) {
        LOG_WARN("Could not load config file (" << ex.what() << "). Using built-in default configuration.");
        cfg = config::ServerConfig::default_config();
    }

    if (port_override > 0) {
        cfg.port = static_cast<uint16_t>(port_override);
    }
    if (!algo_override.empty()) {
        cfg.algorithm = algo_override;
    }

    std::string validation_error;
    if (!cfg.validate(validation_error)) {
        LOG_ERROR("Configuration validation error: " << validation_error);
        return 1;
    }

    LOG_INFO("==================================================================");
    LOG_INFO(" High-Performance C++20 HTTP Reverse Proxy & Load Balancer");
    LOG_INFO(" Listen Host: " << cfg.host << ", Port: " << cfg.port);
    LOG_INFO(" Workers: " << cfg.worker_threads << ", Algorithm: " << cfg.algorithm);
    LOG_INFO(" Backends: " << cfg.backends.size() << " configured nodes");
    LOG_INFO(" Internal Endpoints: /health, /metrics, /backends");
    LOG_INFO("==================================================================");

    g_server_instance = std::make_unique<server::Server>(cfg);
    if (!g_server_instance->start()) {
        LOG_ERROR("Server failed to start.");
        return 1;
    }

    while (!g_shutdown_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    g_server_instance->stop();
    LOG_INFO("Process exiting cleanly.");
    return 0;
}
