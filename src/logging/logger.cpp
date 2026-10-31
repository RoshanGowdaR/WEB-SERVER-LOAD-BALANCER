#include "logging/logger.h"
#include <ctime>
#include <iomanip>

namespace logging {

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::log(Level level, const std::string& message, const char* file, int line) {
    if (level < current_level_) return;

    const char* level_str = "INFO";
    switch (level) {
        case Level::DEBUG: level_str = "DEBUG"; break;
        case Level::INFO:  level_str = "INFO "; break;
        case Level::WARN:  level_str = "WARN "; break;
        case Level::ERROR: level_str = "ERROR"; break;
    }

    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    struct tm timeinfo{};
    localtime_r(&in_time_t, &timeinfo);

    std::ostringstream ss;
    ss << "[" << std::put_time(&timeinfo, "%Y-%m-%d %H:%M:%S")
       << "." << std::setfill('0') << std::setw(3) << ms.count() << "] "
       << "[" << level_str << "] "
       << message;

    if (file != nullptr && level == Level::ERROR) {
        ss << " (" << file << ":" << line << ")";
    }
    ss << "\n";

    std::lock_guard<std::mutex> lock(log_mutex_);
    if (level == Level::ERROR) {
        std::cerr << ss.str() << std::flush;
    } else {
        std::cout << ss.str() << std::flush;
    }
}

} // namespace logging
