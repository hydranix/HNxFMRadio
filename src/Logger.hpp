#pragma once
#include <string>

class Logger {
public:
    enum class Level { DEBUG, INFO, WARN, ERROR };

    static void setLevel(Level l) { level_ = l; }
    static void enableSyslog(bool enable);

    static void debug(const std::string& msg);
    static void info (const std::string& msg);
    static void warn (const std::string& msg);
    static void error(const std::string& msg);

private:
    static void log(Level l, const std::string& msg);

    static Level level_;
    static bool  useSyslog_;
};
