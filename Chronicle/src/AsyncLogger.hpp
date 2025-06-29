#pragma once
#include <atomic>
#include <cassert>
#include <cstdarg>
#include <memory>
#include <mutex>

#include "Level.hpp"
#include "AsyncWorker.hpp"      //后台落盘, log_flush
#include "Message.hpp"
#include "LogFlush.hpp"         //日志输出策略(terminal, file, rollfile...)
#include "../backlogserver/Client.hpp"      //远程备份客户端
#include "ThreadPool.hpp"

extern ThreadPool *thread_pool;

namespace Chronicle {
    //异步日志器, 实现日志的异步生成、格式化和输出
    class AsyncLogger {
    public:
        using ptr = std::shared_ptr<AsyncLogger>;
        //初始化日志器名称、输出策略和异步工作器
        AsyncLogger(const std::string &logger_name, 
            std::vector<LogFlush::ptr> &flushs, 
            AsyncType type):
            _m_logger_name(logger_name),                // 日志器名称
            _m_flushs(flushs.begin(), flushs.end()),    // 写入策略(支持多种)
            //启动异步工作器
            _m_asyncworker(std::make_shared<AsyncWorker>(  
                  std::bind(&AsyncLogger::RealFlush, this, std::placeholders::_1),
                  type)) {}
        virtual ~AsyncLogger() {};
        std::string Name() { return _m_logger_name; }
        //该函数则是特定日志级别的日志信息的格式化，当外部调用该日志器时，使用debug模式的日志就会进来
        //在serialize时把日志信息中的日志级别定义为DEBUG。
        void Debug(const std::string &file, size_t line, const std::string format,
                   ...) {
            // 获取可变参数列表中的格式
            va_list va;
            va_start(va, format);
            char *ret;
            int r = vasprintf(&ret, format.c_str(), va);
            if (r == -1){
                perror("vasprintf failed!!!: ");
            }
            va_end(va); // 将va指针置空

            serialize(LogLevel::value::DEBUG, file, line, ret); // 生成格式化日志信息并写文件

            free(ret);
            ret = nullptr;
        };

        void Info(const std::string &file, size_t line, const std::string format,
                  ...) {
            va_list va;
            va_start(va, format);
            char *ret;
            int r = vasprintf(&ret, format.c_str(), va);
            if (r == -1){
                perror("vasprintf failed!!!: ");
            }
            va_end(va);

            serialize(LogLevel::value::INFO, file, line, ret);

            free(ret);
            ret = nullptr;
        };

        void Warn(const std::string &file, size_t line, const std::string format,
                  ...) {
            va_list va;
            va_start(va, format);
            char *ret;
            int r = vasprintf(&ret, format.c_str(), va);
            if (r == -1){
                perror("vasprintf failed!!!: ");
            }
            va_end(va);

            serialize(LogLevel::value::WARN, file, line, ret);

            free(ret);
            ret = nullptr;
        };

        void Error(const std::string &file, size_t line, const std::string format,
                   ...) {
            va_list va;
            va_start(va, format);
            char *ret;
            int r = vasprintf(&ret, format.c_str(), va);
            if (r == -1){
                perror("vasprintf failed!!!: ");
            }
            va_end(va);

            serialize(LogLevel::value::ERROR, file, line, ret);

            free(ret);
            ret = nullptr;
        };
        
        void Fatal(const std::string &file, size_t line, const std::string format,
                   ...) {
            va_list va;
            va_start(va, format);
            char *ret;
            int r = vasprintf(&ret, format.c_str(), va);
            if (r == -1){
                perror("vasprintf failed!!!: ");
            }
            va_end(va);

            serialize(LogLevel::value::FATAL, file, line, ret);

            free(ret);
            ret = nullptr;
        };

    protected:
        // 序列化日志消息并处理输出
        void serialize(LogLevel::value level, const std::string &file, size_t line,
                       char *ret_future) {
            LogMessage msg(level, file, line, _m_logger_name, ret_future);
            // 获取具体的log内容行
            std::string data = msg.format();
            //远程备份ERROR、FATAL日志
            if (level == LogLevel::value::FATAL || level == LogLevel::value::ERROR){
                try{
                    auto ret_future = thread_pool->enqueue(start_backup, data);
                    ret_future.get();   //无返回值, 这里实际上是等待异步执行结束, 即等待连接客户端发送msg给socket并断开socket
                }
                catch (const std::runtime_error &e){
                    std::cout << __FILE__ << " " << __LINE__ << " thread pool runtime error" << std::endl;
                }
            }
            // 将日志数据推送到异步缓冲区, AsyncWoker自动调用回调函数处理缓冲区
            PushToBuffer(data.c_str(), data.size());

            /*业务线程调用Push()将日志数据放入生产者缓冲区后，立即返回继续执行，而实际刷盘操作由后台线程异步处理*/
            // std::cout << "Debug:serialize Flush\n";
        }

        // 推送日志数据到异步工作器, 由AsyncWorker的回调函数实现日志落地
        // 由AsyncWorker保证线程安全, 这里不需要加锁
        void PushToBuffer(const char *data, size_t len) {
            _m_asyncworker->Push(data, len); 
        }

        // 日志数据的回调函数, AsyncWorker._m_callback_func
        // 由异步线程进行实际写文件
        void RealFlush(Buffer &buffer) {
            if (_m_flushs.empty()){
                return;
            }
            // 遍历所有输出策略并执行刷盘
            for (auto &e : _m_flushs){
                e->Flush(buffer.Begin(), buffer.ReadableSize());
            }
        }

    protected:
        std::mutex _m_mtx;
        std::string _m_logger_name;
        std::vector<LogFlush::ptr> _m_flushs;   //用LogFlush子类实例化
        // std::vector<LogFlush> flush_;不能使用logflush作为元素类型，logflush是纯虚类，不能实例化
        Chronicle::AsyncWorker::ptr _m_asyncworker;
    };

    // 日志器建造
    class LoggerBuilder {
    public:
        using ptr = std::shared_ptr<LoggerBuilder>;
        void SetLoggerName(const std::string &name) { _m_logger_name = name; }
        // 缓冲区增长方式: 不增长(ASYNC_SAFE)、增长(UNSAFE, for debug)
        void SetLopperType(AsyncType type) { _m_async_type = type; }
        
        //添加写日志方式(可添加多种)
        template <typename FlushType, typename... Args>
        void BuildLoggerFlush(Args &&...args) {
            _m_flushs.emplace_back(
                LogFlushFactory::CreateLog<FlushType>(std::forward<Args>(args)...));
        }

        //根据LoggerBuilder生成一个Logger
        AsyncLogger::ptr BuildLogger() {
            // 必须有日志器名称
            assert(_m_logger_name.empty() == false);
            
            // 如果写日志方式没有指定，那么采用默认的标准输出
            if (_m_flushs.empty()){
                _m_flushs.emplace_back(std::make_shared<StdoutFlush>());
            }
            return std::make_shared<AsyncLogger>(
                _m_logger_name, _m_flushs, _m_async_type);
        }

    protected:
        std::string _m_logger_name = "async_logger";        // 日志器名称
        std::vector<Chronicle::LogFlush::ptr> _m_flushs;    // 写日志方式
        AsyncType _m_async_type = AsyncType::ASYNC_SAFE;      // 用于控制缓冲区是否增长
    };
} // namespace Chronicle