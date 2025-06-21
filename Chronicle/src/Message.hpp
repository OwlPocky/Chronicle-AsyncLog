#pragma once

#include <memory>
#include <thread>

#include "Level.hpp"
#include "Util.hpp"

#include <sstream>

namespace Chronicle {
    struct LogMessage{
        using ptr = std::shared_ptr<LogMessage>;
        LogMessage() = default;
        LogMessage(LogLevel::value level, std::string file, size_t line,
                std::string name, std::string payload) : 
                    _m_name(name),
                    _m_ctime(Util::Date::Now()),
                    _m_file_name(file),
                    _m_line(line),
                    _m_tid(std::this_thread::get_id()),
                    _m_level(level),
                    _m_payload(payload) {}
        std::string format() {
            std::stringstream ret;
            // 获取当前时间
            struct tm t;
            localtime_r(&_m_ctime, &t);
            char buf[128];
            strftime(buf, sizeof(buf), "%H:%M:%S", &t);

            // 格式化线程ID为16进制
            std::stringstream tid_ss;
            tid_ss << "0x" << std::hex << std::hash<std::thread::id>()(_m_tid);
            std::string tid_str = tid_ss.str();
            
            // 构建完整日志格式
            std::string tmp1 = '[' + std::string(buf) + "][" + tid_str + "][";
            std::string tmp2 = std::string(LogLevel::ToString(_m_level)) + "][" + _m_name + "][" + _m_file_name + ":" + std::to_string(_m_line) + "]\t" + _m_payload + "\n";
            
            ret << tmp1 << tmp2;
            return ret.str();
        }

        std::string _m_name;        // 日志器名称
        time_t _m_ctime;            // 代码执行时间戳
        std::string _m_file_name;   // 源文件名
        size_t _m_line;             // 代码行号
        std::thread::id _m_tid;     // 线程id
        LogLevel::value _m_level;   // 日志级别
        std::string _m_payload;     // 日志内容
    };
} // namespace Chronicle