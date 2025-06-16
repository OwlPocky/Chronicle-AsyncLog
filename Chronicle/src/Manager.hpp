#include <unordered_map>
#include "AsyncLogger.hpp"

/*
    日志管理器, 单例模式(懒汉式)
    负责创建和管理多个异步日志
*/
namespace Chronicle {
    class LoggerManager {
    public:
        static LoggerManager& GetInstance() {
            static LoggerManager lm;
            return lm;
        }

        //通过name获取异步日志器
        AsyncLogger::ptr GetLogger(const std::string &name) {
            std::unique_lock<std::mutex> lock(_m_mtx);
            auto it = _m_logs.find(name);
            if (it == _m_logs.end()){
                return AsyncLogger::ptr();  //std::shared_ptr<AsyncLogger>(), .get() == nullptr
            }
            return it->second;
        }

        //添加日志器至_m_logs, 传入右值引用
        void AddLogger(const AsyncLogger::ptr &&AsyncLogger) {
            //printf("AddLogger()\n");
            if (LoggerExist(AsyncLogger->Name())) {
                return;
            }
            std::unique_lock<std::mutex> lock(_m_mtx);
            _m_logs.insert(std::make_pair(AsyncLogger->Name(), AsyncLogger));
        }

        AsyncLogger::ptr DefaultLogger() { 
            return _m_default_logger; 
        }

    private:
        //初次调用GetInstance()创建default Logger
        LoggerManager() {
            std::unique_ptr<LoggerBuilder> builder(new LoggerBuilder());
            builder->SetLoggerName("default");
            _m_default_logger = builder->BuildLogger();
            _m_logs.insert(std::make_pair("default", _m_default_logger));
            //AddLogger(std::move(_m_default_logger));
        }

        bool LoggerExist(const std::string &name) {
            std::unique_lock<std::mutex> lock(_m_mtx);
            auto it = _m_logs.find(name);
            if (it == _m_logs.end()){
                return false;
            }
            return true;
        }


    private:
        std::mutex _m_mtx;
        AsyncLogger::ptr _m_default_logger;
        std::unordered_map<std::string, AsyncLogger::ptr> _m_logs; 
    };
}