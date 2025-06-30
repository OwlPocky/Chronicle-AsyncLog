#define DEBUG_LOG
#include "Service.hpp"
#include <thread>
using namespace std;

storage::DataManager *data_mgr;
ThreadPool* thread_pool = nullptr;
Chronicle::Util::JsonData* g_conf_data;

void service_module(){
    storage::Service s;
    Chronicle::GetLogger("asynclogger")->Info("service step in Run()");
    s.Run();
}

void Chronicle_module_init(){
    // Chronicle本地备份, 192.168.206.136:8085
    g_conf_data = Chronicle::Util::JsonData::GetJsonData();
    thread_pool = new ThreadPool(g_conf_data->thread_count);
    std::shared_ptr<Chronicle::LoggerBuilder> CLoggerBuilder(new Chronicle::LoggerBuilder());
    CLoggerBuilder->SetLoggerName("asynclogger");
    CLoggerBuilder->BuildLoggerFlush<Chronicle::RollFileFlush>("./logfile/RollFile_log_",
                                              1024 * 1024);
    // The LoggerManger has been built and is managed by members of the LoggerManger class
    // The logger is assigned to the managed object, and the caller lands the log by invoking the singleton managed object
    Chronicle::LoggerManager::GetInstance().AddLogger(CLoggerBuilder->BuildLogger());
}

int main(){
    Chronicle_module_init();
    data_mgr = new storage::DataManager();

    thread t1(service_module);

    t1.join();
    delete(thread_pool);
    return 0;
}