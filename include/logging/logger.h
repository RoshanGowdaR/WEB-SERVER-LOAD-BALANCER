#pragma once

#include <string>
#include <mutex>
#include <iostream>
#include <sstream>
#include <chrono>

namespace logging {

enum class Level {
    DEBUG,
    INFO,
    WARN,
    ERROR
};

class Logger {
public:
    static Logger& instance();

    void set_level(Level level) noexcept { current_level_ = level; }
    [[nodiscard]] Level get_level() const noexcept { return current_level_; }

    void log(Level level, const std::string& message, const char* file = nullptr, int line = 0);

private:
    Logger() = default;
    ~Logger() = default;

    Level current_level_{Level::INFO};
    std::mutex log_mutex_;
};

#define LOG_DEBUG(msg) do { \
    if (::logging::Logger::instance().get_level() <= ::logging::Level::DEBUG) { \
        std::ostringstream _oss; _oss << msg; \
        ::logging::Logger::instance().log(::logging::Level::DEBUG, _oss.str(), __FILE__, __LINE__); \
    } \
} while(0)

#define LOG_INFO(msg) do { \
    if (::logging::Logger::instance().get_level() <= ::logging::Level::INFO) { \
        std::ostringstream _oss; _oss << msg; \
        ::logging::Logger::instance().log(::logging::Level::INFO, _oss.str()); \
    } \
} while(0)

#define LOG_WARN(msg) do { \
    if (::logging::Logger::instance().get_level() <= ::logging::Level::WARN) { \
        std::ostringstream _oss; _oss << msg; \
        ::logging::Logger::instance().log(::logging::Level::WARN, _oss.str()); \
    } \
} while(0)

#define LOG_ERROR(msg) do { \
    if (::logging::Logger::instance().get_level() <= ::logging::Level::ERROR) { \
        std::ostringstream _oss; _oss << msg; \
        ::logging::Logger::instance().log(::logging::Level::ERROR, _oss.str(), __FILE__, __LINE__); \
    } \
} while(0)

} // namespace logging
