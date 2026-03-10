#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <memory>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <iostream>

namespace geweinet {

/**
 * 日志级别
 */
enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error,
    Fatal
};

/**
 * 日志记录器
 * 支持多线程安全的日志输出，可同时输出到文件和终端
 */
class Logger {
public:
    static Logger& instance();
    
    void set_level(LogLevel level) { level_ = level; }
    void set_log_file(const std::string& path);
    
    void debug(const std::string& message, const std::string& component = "");
    void info(const std::string& message, const std::string& component = "");
    void warning(const std::string& message, const std::string& component = "");
    void error(const std::string& message, const std::string& component = "");
    void fatal(const std::string& message, const std::string& component = "");
    
    void log(LogLevel level, const std::string& message, const std::string& component = "");

private:
    Logger() = default;
    ~Logger();
    
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    std::string format_message(LogLevel level, const std::string& message, const std::string& component);
    std::string level_to_string(LogLevel level);
    std::string get_timestamp();
    
    LogLevel level_ = LogLevel::Info;
    std::ofstream file_stream_;
    std::mutex mutex_;
    bool file_enabled_ = false;
};

// 便捷宏
#define LOG_DEBUG(msg) geweinet::Logger::instance().debug(msg, __func__)
#define LOG_INFO(msg) geweinet::Logger::instance().info(msg, __func__)
#define LOG_WARNING(msg) geweinet::Logger::instance().warning(msg, __func__)
#define LOG_ERROR(msg) geweinet::Logger::instance().error(msg, __func__)
#define LOG_FATAL(msg) geweinet::Logger::instance().fatal(msg, __func__)

#define LOG_COMPONENT_DEBUG(msg, comp) geweinet::Logger::instance().debug(msg, comp)
#define LOG_COMPONENT_INFO(msg, comp) geweinet::Logger::instance().info(msg, comp)
#define LOG_COMPONENT_WARNING(msg, comp) geweinet::Logger::instance().warning(msg, comp)
#define LOG_COMPONENT_ERROR(msg, comp) geweinet::Logger::instance().error(msg, comp)
#define LOG_COMPONENT_FATAL(msg, comp) geweinet::Logger::instance().fatal(msg, comp)

} // namespace geweinet
