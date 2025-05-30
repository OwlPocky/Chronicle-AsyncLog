#include <iostream>
#include <string>
#include <ctime>
#include <fstream>
#include <memory>

// 日志级别枚举
enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL
};

// 日志格式化器
class Formatter {
public:
    std::string format(LogLevel level, const std::string& message) {
        std::time_t now = std::time(nullptr);
        std::string time_str = std::ctime(&now);
        time_str.pop_back(); // 去掉换行符

        std::string m_level_str;
        switch (level) {
            case LogLevel::DEBUG: m_level_str = "DEBUG"; break;
            case LogLevel::INFO: m_level_str = "INFO"; break;
            case LogLevel::WARN: m_level_str = "WARN"; break;
            case LogLevel::ERROR: m_level_str = "ERROR"; break;
            case LogLevel::FATAL: m_level_str = "FATAL"; break;
        }

        return "[" + time_str + "] [" + m_level_str + "] " + message;
    }
};

// 日志输出器
class Flush {
public:
    virtual void flush(const std::string& formatted_log) = 0;
    virtual ~Flush() = default;
};

// 控制台输出器
class ConsoleFlush : public Flush {
public:
    void flush(const std::string& formatted_log) override {
        std::cout << formatted_log << std::endl;
    }
};

// 文件输出器
class FileFlush : public Flush {
public:
    FileFlush(const std::string& filename) : m_file(filename, std::ios::app) {}

    void flush(const std::string& formatted_log) override {
        if (m_file.is_open()) {
            m_file << formatted_log << std::endl;
        }
    }

    ~FileFlush() {
        if (m_file.is_open()) {
            m_file.close();
        }
    }

private:
    std::ofstream m_file;
};

// 日志记录器
class Logger {
public:
    Logger(LogLevel level) : _m_level(level), _m_formatter(std::make_unique<Formatter>()) {
        _m_console_Flush = std::make_unique<ConsoleFlush>();
        _m_file_Flush = std::make_unique<FileFlush>("app.log");
    }

    void debug(const std::string& message) {
        log(LogLevel::DEBUG, message);
    }

    void info(const std::string& message) {
        log(LogLevel::INFO, message);
    }

    void warn(const std::string& message) {
        log(LogLevel::WARN, message);
    }

    void error(const std::string& message) {
        log(LogLevel::ERROR, message);
    }

    void fatal(const std::string& message) {
        log(LogLevel::FATAL, message);
    }

private:
    void log(LogLevel level, const std::string& message) {
        if (level >= _m_level) {
            std::string formatted_log = _m_formatter->format(level, message);
            _m_console_Flush->flush(formatted_log);
            _m_file_Flush->flush(formatted_log);
        }
    }
    
    LogLevel _m_level;
    std::unique_ptr<Formatter> _m_formatter;
    std::unique_ptr<Flush> _m_console_Flush;
    std::unique_ptr<Flush> _m_file_Flush;
};

int main() {
    Logger logger(LogLevel::INFO);
    logger.debug("This is a debug message");
    logger.info("This is an info message");
    logger.warn("This is a warning message");
    logger.error("This is an error message");
    logger.fatal("This is a fatal message");
    return 0;
}