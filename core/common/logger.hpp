#pragma once

#include <string>
#include <sstream>
#include <iostream>
#include <mutex>
#include <chrono>
#include <iomanip>

namespace dicp {

enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3,
    FATAL = 4
};

class Logger {
public:
    static Logger& Instance() {
        static Logger instance;
        return instance;
    }

    void SetLogLevel(LogLevel level) {
        level_ = level;
    }

    LogLevel GetLogLevel() const {
        return level_;
    }

    void SetNodeId(const std::string& node_id) {
        node_id_ = node_id;
    }

    void Log(LogLevel level, const std::string& component, const std::string& message) {
        if (level < level_) return;

        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        std::tm tm_buf;
        gmtime_r(&in_time_t, &tm_buf);

        std::lock_guard<std::mutex> lock(mutex_);
        std::ostream& out = (level >= LogLevel::ERROR) ? std::cerr : std::cout;

        out << "{\"time\":\""
            << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S")
            << '.' << std::setfill('0') << std::setw(3) << ms.count() << "Z\""
            << ",\"level\":\"" << LevelToString(level) << "\""
            << (!node_id_.empty() ? ",\"node_id\":\"" + node_id_ + "\"" : "")
            << ",\"component\":\"" << component << "\""
            << ",\"msg\":\"" << EscapeJson(message) << "\"}"
            << std::endl;
    }

private:
    Logger() : level_(LogLevel::INFO) {}

    static const char* LevelToString(LogLevel level) {
        switch (level) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO:  return "INFO";
            case LogLevel::WARN:  return "WARN";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::FATAL: return "FATAL";
            default: return "UNKNOWN";
        }
    }

    static std::string EscapeJson(const std::string& s) {
        std::ostringstream o;
        for (char c : s) {
            switch (c) {
                case '"': o << "\\\""; break;
                case '\\': o << "\\\\"; break;
                case '\b': o << "\\b"; break;
                case '\f': o << "\\f"; break;
                case '\n': o << "\\n"; break;
                case '\r': o << "\\r"; break;
                case '\t': o << "\\t"; break;
                default:
                    if ('\x00' <= c && c <= '\x1f') {
                        o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                    } else {
                        o << c;
                    }
            }
        }
        return o.str();
    }

    LogLevel level_;
    std::string node_id_;
    std::mutex mutex_;
};

#define DICP_LOG_DEBUG(comp, msg) dicp::Logger::Instance().Log(dicp::LogLevel::DEBUG, comp, msg)
#define DICP_LOG_INFO(comp, msg)  dicp::Logger::Instance().Log(dicp::LogLevel::INFO,  comp, msg)
#define DICP_LOG_WARN(comp, msg)  dicp::Logger::Instance().Log(dicp::LogLevel::WARN,  comp, msg)
#define DICP_LOG_ERROR(comp, msg) dicp::Logger::Instance().Log(dicp::LogLevel::ERROR, comp, msg)
#define DICP_LOG_FATAL(comp, msg) dicp::Logger::Instance().Log(dicp::LogLevel::FATAL, comp, msg)

} // namespace dicp
