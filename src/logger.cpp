#include "geweinet/logger.hpp"
#include <iostream>
#include <ctime>

namespace geweinet {

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

Logger::~Logger() {
    if (file_stream_.is_open()) {
        file_stream_.close();
    }
}

void Logger::set_log_file(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_stream_.is_open()) {
        file_stream_.close();
    }
    file_stream_.open(path, std::ios::app);
    file_enabled_ = file_stream_.is_open();
    if (!file_enabled_) {
        std::cerr << "Failed to open log file: " << path << std::endl;
    }
}

std::string Logger::get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::stringstream ss;
    // 使用 localtime_r 保证线程安全
    struct tm tm_result;
    localtime_r(&time, &tm_result);
    ss << std::put_time(&tm_result, "%Y-%m-%d %H:%M:%S");
    ss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

std::string Logger::level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO";
        case LogLevel::Warning: return "WARN";
        case LogLevel::Error:   return "ERROR";
        case LogLevel::Fatal:   return "FATAL";
        default:                return "UNKNOWN";
    }
}

std::string Logger::format_message(LogLevel level, const std::string& message, const std::string& component) {
    std::stringstream ss;
    ss << "[" << get_timestamp() << "] ";
    ss << "[" << std::setw(5) << level_to_string(level) << "] ";
    if (!component.empty()) {
        ss << "[" << component << "] ";
    }
    ss << message;
    return ss.str();
}

void Logger::log(LogLevel level, const std::string& message, const std::string& component) {
    if (level < level_) {
        return;
    }
    
    std::string formatted = format_message(level, message, component);
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 输出到终端
    if (level >= LogLevel::Error) {
        std::cerr << formatted << std::endl;
    } else {
        std::cout << formatted << std::endl;
    }
    
    // 输出到文件
    if (file_enabled_ && file_stream_.is_open()) {
        file_stream_ << formatted << std::endl;
        file_stream_.flush();
    }
}

void Logger::debug(const std::string& message, const std::string& component) {
    log(LogLevel::Debug, message, component);
}

void Logger::info(const std::string& message, const std::string& component) {
    log(LogLevel::Info, message, component);
}

void Logger::warning(const std::string& message, const std::string& component) {
    log(LogLevel::Warning, message, component);
}

void Logger::error(const std::string& message, const std::string& component) {
    log(LogLevel::Error, message, component);
}

void Logger::fatal(const std::string& message, const std::string& component) {
    log(LogLevel::Fatal, message, component);
}

} // namespace geweinet
