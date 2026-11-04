#include "server/server.h"
#include "config/config.h"
#include "logging/logger.h"
#include <iostream>
#include <csignal>
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <charconv>

static std::atomic<bool> g_shutdown_requested{false};
static std::unique_ptr<server::Server> g_server_instance{nullptr};
static std::thread g_signal_thread;

void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [options]\n"
              << "Options:\n"
              << "  -c, --config <file>    Path to JSON configuration file (default: config/config.example.json)\n"
              << "  -p, --port <port>      Override listening port (1-65535)\n"
              << "  -a, --algo <algorithm> Load balancing algorithm ('round_robin' or 'least_connections')\n"
              << "  -v, --verbose          Enable DEBUG level logging\n"
              << "  -h, --help             Display this help message\n";
}

int main(int argc, char* argv[]) {
    // Block SIGINT and SIGTERM before creating any threads so all threads inherit the signal mask
    sigset_t signal_set;
    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGINT);
    sigaddset(&signal_set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &signal_set, nullptr);

    std::string config_path = "config/config.example.json";
    int port_override = -1;
    std::string algo_override;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "-c" || arg == "--config") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --config requires a file path argument.\n";
                return 1;
            }
            config_path = argv[++i];
        } else if (arg == "-p" || arg == "--port") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --port requires a port number argument.\n";
                return 1;
            }
            std::string port_str = argv[++i];
            int p = 0;
            auto [ptr, ec] = std::from_chars(port_str.data(), port_str.data() + port_str.size(), p);
            if (ec != std::errc{} || ptr != port_str.data() + port_str.size() || p < 1 || p > 65535) {
                std::cerr << "Error: Invalid port number '" << port_str << "'. Must be an integer between 1 and 65535.\n";
                return 1;
            }
            port_override = p;
        } else if (arg == "-a" || arg == "--algo") {
            if (i + 1 >= argc) {
                std::cerr << "Error: --algo requires an algorithm name ('round_robin' or 'least_connections').\n";
                return 1;
            }
            algo_override = argv[++i];
            if (algo_override != "round_robin" && algo_override != "least_connections") {
                std::cerr << "Error: Unknown load balancing algorithm '" << algo_override 
                          << "'. Must be 'round_robin' or 'least_connections'.\n";
                return 1;
            }
        } else if (arg == "-v" || arg == "--verbose") {
            logging::Logger::instance().set_level(logging::Level::DEBUG);
        } else {
            std::cerr << "Error: Unknown argument '" << arg << "'. Use --help for usage.\n";
            return 1;
        }
    }

    // Fail-fast configuration loading
    config::ServerConfig cfg;
    try {
        cfg = config::ServerConfig::load_from_file(config_path);
        LOG_INFO("Loaded configuration from: " << config_path);
    } catch (const std::exception& ex) {
        LOG_ERROR("Failed to load configuration file (" << config_path << "): " << ex.what());
        return 1;
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

    // Dedicated signal monitoring thread using POSIX sigwait (100% async-signal safe)
    g_signal_thread = std::thread([signal_set]() {
        int sig = 0;
        int res = sigwait(&signal_set, &sig);
        if (res == 0) {
            LOG_INFO("Signal " << sig << " (SIGINT/SIGTERM) received. Initiating graceful shutdown...");
            g_shutdown_requested.store(true);
            if (g_server_instance) {
                g_server_instance->stop();
            }
        }
    });

    if (!g_server_instance->start()) {
        LOG_ERROR("Server failed to start.");
        // Unblock signal thread by sending SIGTERM to self and exit
        pthread_kill(pthread_self(), SIGTERM);
        if (g_signal_thread.joinable()) g_signal_thread.join();
        return 1;
    }

    // Wait until shutdown is requested and server stops
    g_server_instance->wait();

    if (g_signal_thread.joinable()) {
        // If server stopped on its own (not by signal), raise signal to unblock sigwait
        if (!g_shutdown_requested.load()) {
            pthread_kill(g_signal_thread.native_handle(), SIGTERM);
        }
        g_signal_thread.join();
    }

    LOG_INFO("Shutdown completed. Process exiting cleanly.");
    return 0;
}
