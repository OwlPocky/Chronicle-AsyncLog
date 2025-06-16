#include "../src/Chronicle.hpp"
#include "../src/ThreadPool.hpp"
#include "../src/Util.hpp"
using std::cout;
using std::endl;

ThreadPool* thread_pool = nullptr;
Chronicle::Util::JsonData* g_conf_data;
void test() {
    int cur_size = 0;
    int cnt = 1;
    while (cur_size++ < 2) {
        cout << "cur_size: " << cur_size << endl;
        Chronicle::GetLogger("asynclogger")->Info("测试日志-%d", cnt++);
        Chronicle::GetLogger("asynclogger")->Warn("测试日志-%d", cnt++);
        Chronicle::GetLogger("asynclogger")->Debug("测试日志-%d", cnt++);
        Chronicle::GetLogger("asynclogger")->Error("测试日志-%d", cnt++);
        Chronicle::GetLogger("asynclogger")->Fatal("测试日志-%d", cnt++);
    }
}

void init_thread_pool() {
    thread_pool = new ThreadPool(g_conf_data->thread_count);
}
int main() {
    g_conf_data = Chronicle::Util::JsonData::GetJsonData();
    init_thread_pool();
    std::shared_ptr<Chronicle::LoggerBuilder> CLoggerBuilder(new Chronicle::LoggerBuilder());
    CLoggerBuilder->SetLoggerName("asynclogger");
    //CLoggerBuilder->BuildLoggerFlush<Chronicle::FileFlush>("./test1/test2/test3/logfile/FileFlush.log");
    //CLoggerBuilder->BuildLoggerFlush<Chronicle::RollFileFlush>("./test1/test2/test3/logfile/RollFile_log", 1024 * 1024); 
    //写日志方式
    CLoggerBuilder->BuildLoggerFlush<Chronicle::FileFlush>("./logfile/FileFlush.log");
    CLoggerBuilder->BuildLoggerFlush<Chronicle::RollFileFlush>("./logfile/RollFile_log", 1024 * 1024);

    // 日志器参数已经设置完成，由LoggerManger类成员管理所有日志器
    // 调用者通过调用单例LoggerManager对象对日志进行落盘
    Chronicle::LoggerManager::GetInstance().AddLogger(CLoggerBuilder->BuildLogger());
    cout << "------------" << endl;
    test();
    
    delete(thread_pool);
    return 0;
}