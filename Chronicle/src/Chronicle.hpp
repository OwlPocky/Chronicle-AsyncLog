#pragma once
#include "Manager.hpp"
namespace Chronicle {
    // 用户获取日志器
    AsyncLogger::ptr GetLogger(const std::string &name) {
        return LoggerManager::GetInstance().GetLogger(name);
    }
    // 用户获取默认日志器
    AsyncLogger::ptr DefaultLogger() { 
        return LoggerManager::GetInstance().DefaultLogger(); 
    }

    // 简化用户使用，宏函数默认填上文件吗+行号
    #define Debug(fmt, ...) Debug(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
    #define Info(fmt, ...)  Info(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
    #define Warn(fmt, ...)  Warn(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
    #define Error(fmt, ...) Error(__FILE__, __LINE__, fmt, ##__VA_ARGS__)
    #define Fatal(fmt, ...) Fatal(__FILE__, __LINE__, fmt, ##__VA_ARGS__)

    // 无需获取日志器，默认标准输出
    #define LOG_DEBUG_DEFAULT(fmt, ...) Chronicle::DefaultLogger()->Debug(fmt, ##__VA_ARGS__)
    #define LOG_INFO_DEFAULT(fmt, ...)  Chronicle::DefaultLogger()->Info(fmt, ##__VA_ARGS__)
    #define LOG_WARN_DEFAULT(fmt, ...)  Chronicle::DefaultLogger()->Warn(fmt, ##__VA_ARGS__)
    #define LOG_ERROR_DEFAULT(fmt, ...) Chronicle::DefaultLogger()->Error(fmt, ##__VA_ARGS__)
    #define LOG_FATAL_DEFAULT(fmt, ...) Chronicle::DefaultLogger()->Fatal(fmt, ##__VA_ARGS__)
}  // namespace Chronicle