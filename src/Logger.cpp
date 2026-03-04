#include "Logger.hpp"

#include <iostream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <syslog.h>

Logger::Level Logger::level_    = Logger::Level::INFO;
bool          Logger::useSyslog_ = false;

static std::mutex g_logMutex;

static std::string timestamp() {
    auto now  = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream ss;
    ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

void Logger::enableSyslog(bool enable) {
    useSyslog_ = enable;
    if (enable) openlog("hnxfmradiod", LOG_PID | LOG_CONS, LOG_DAEMON);
    else         closelog();
}

void Logger::debug(const std::string& msg) { log(Level::DEBUG, msg); }
void Logger::info (const std::string& msg) { log(Level::INFO,  msg); }
void Logger::warn (const std::string& msg) { log(Level::WARN,  msg); }
void Logger::error(const std::string& msg) { log(Level::ERROR, msg); }

void Logger::log(Level l, const std::string& msg) {
    if (l < level_) return;

    static const char* labels[] = { "DEBUG", "INFO ", "WARN ", "ERROR" };
    int idx = static_cast<int>(l);

    std::lock_guard<std::mutex> lk(g_logMutex);
    std::cout << "[" << timestamp() << "] [" << labels[idx] << "] " << msg << "\n";
    std::cout.flush();

    if (useSyslog_) {
        static const int priorities[] = { LOG_DEBUG, LOG_INFO, LOG_WARNING, LOG_ERR };
        syslog(priorities[idx], "%s", msg.c_str());
    }
}
