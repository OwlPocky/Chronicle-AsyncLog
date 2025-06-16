#include <cassert>
#include <fstream>
#include <memory>
#include <unistd.h>
#include "Util.hpp"

extern Chronicle::Util::JsonData* g_conf_data;
namespace Chronicle {
    //日志输出策略:
    //  StdoutFlush:    日志输出到标准输出(控制台)
    //  FileFlush:      日志写入固定文件，支持不同刷盘策略(由flush_log决定)
    //  RollFileFlush:  日志滚动写入文件(按大小或时间分割)
    class LogFlush {
    public:
        using ptr = std::shared_ptr<LogFlush>;
        virtual ~LogFlush() {}
        //不同的输出方式, 需要override Flush
        virtual void Flush(const char *data, size_t len) = 0;
    };

    //日志输出到标准输出(控制台)
    class StdoutFlush : public LogFlush {
    public:
        using ptr = std::shared_ptr<StdoutFlush>;
        void Flush(const char *data, size_t len) override{
            cout.write(data, len);
        }
    };

    //日志写入固定文件，支持不同刷盘策略(由flush_log决定)
    //  flush_log == 1: 执行fflush将数据从用户缓冲区刷新到内核, 不强制将缓冲区的内容同步到磁盘
    //  flush_log == 2: 执行fflush将数据从用户缓冲区刷新到内核, 强制将缓冲区的内容同步到磁盘, 影响性能
    //  flush_log == 0: default, 仅执行write将数据写入用户缓冲区, 不执行fflush刷新到内核、不执行fsync写入硬盘
    class FileFlush : public LogFlush {
    public:
        using ptr = std::shared_ptr<FileFlush>;
        FileFlush(const std::string &filename): _m_filename(filename) {
            // 检查目录是否存在, 不存在则创建
            Util::File::CreateDirectory(Util::File::Path(filename));
            cout << "FileFlush Path " << Util::File::Path(filename) << endl;
            // 打开文件
            _m_fs = fopen(filename.c_str(), "ab");  //append binary
            if(_m_fs == NULL){
                std::cout << __FILE__ << " " << __LINE__ << " open log file failed"<< std::endl;
                perror(NULL);
            }
        }
        void Flush(const char *data, size_t len) override {
            //写数据流向: ptr->stream, 大小: size(元素大小) * nmemb(元素数量)
            //size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);
            //1. 数据写入_m_fs的用户缓冲区
            fwrite(data, 1, len, _m_fs);    //data->file
            if(ferror(_m_fs)){
                std::cout << __FILE__ << " " << __LINE__ << " write log file failed"<< std::endl;
                perror(NULL);
            }
            if(g_conf_data->flush_log == 1){
                //2. 用户缓冲区刷新到内核缓冲区
                if(fflush(_m_fs) == EOF){
                    std::cout << __FILE__ << " " << __LINE__ << " fflush file failed"<< std::endl;
                    perror(NULL);
                }
            }
            else if(g_conf_data->flush_log == 2){
                //2. 用户缓冲区刷新到内核缓冲区
                fflush(_m_fs);
                //3. 内核缓冲区数据强制写入硬盘, 触发系统调用fsync
                fsync(fileno(_m_fs));
            }
        }

    private:
        std::string _m_filename;
        FILE* _m_fs = NULL; 
    };

    //日志滚动写入文件(按文件大小分割)
    //  文件大小分割: 当文件日志大小大于_m_max_size时, 自动创建新文件
    class RollFileFlush : public LogFlush {
    public:
        using ptr = std::shared_ptr<RollFileFlush>;
        RollFileFlush(const std::string &filename, size_t max_size)
            : _m_max_size(max_size), _m_filename(filename) {
            Util::File::CreateDirectory(Util::File::Path(filename));
        }

        void Flush(const char *data, size_t len) override {
            // 确认文件大小不满足滚动需求
            InitLogFile();
            // 向文件写入内容
            fwrite(data, 1, len, _m_fs);
            if(ferror(_m_fs)){
                std::cout << __FILE__ << " " << __LINE__ << " write log file failed"<< std::endl;
                perror(NULL);
            }
            _m_cur_size += len;
            if(g_conf_data->flush_log == 1){
                if(fflush(_m_fs)){
                    std::cout << __FILE__ << " " << __LINE__ << " fflush file failed"<< std::endl;
                    perror(NULL);
                }
            }else if(g_conf_data->flush_log == 2){
                fflush(_m_fs);
                fsync(fileno(_m_fs));
            }
        }

    private:
        //初始化一个新文件, 初始化时机: 文件满触发新滚动、刚启动时
        void InitLogFile() {
            // 文件不存在或已达最大大小时触发滚动
            if (_m_fs==NULL || _m_cur_size >= _m_max_size) {
                // 关闭已打开的文件(可能由于文件满触发滚动)
                if(_m_fs!=NULL){
                    fclose(_m_fs);
                    _m_fs=NULL;
                }   
                std::string filename = CreateFilename();
                _m_fs=fopen(filename.c_str(), "ab");
                if(_m_fs==NULL){
                    std::cout << __FILE__ << " " << __LINE__ << " open file failed"<< std::endl;
                    perror(NULL);
                }
                _m_cur_size = 0;
            }
        }

        // 构建落地的滚动日志文件名称, 实际年月日+小时分钟秒数.log
        std::string CreateFilename() {
            time_t time_ = Util::Date::Now();
            struct tm t;
            localtime_r(&time_, &t);
            std::string filename = _m_filename;
            filename += std::to_string(t.tm_year + 1900);
            filename += std::to_string(t.tm_mon + 1);
            filename += std::to_string(t.tm_mday);
            filename += std::to_string(t.tm_hour);
            filename += std::to_string(t.tm_min);
            filename += std::to_string(t.tm_sec) + '-' +
                        std::to_string(_m_cnt++) + ".log";
            return filename;
        }

    private:
        size_t _m_cnt = 1;
        size_t _m_cur_size = 0;
        size_t _m_max_size;
        std::string _m_filename;
        // std::ofstream;
        FILE* _m_fs = NULL;
    };

    //工厂类, 静态工具类
    //只涉及静态调用, 通过class::func()静态调用
    class LogFlushFactory {
    public:
        //using ptr = std::shared_ptr<LogFlushFactory>;
        template <typename FlushType, typename... Args>
        static std::shared_ptr<LogFlush> CreateLog(Args &&...args) {
            return std::make_shared<FlushType>(std::forward<Args>(args)...);
        }
    };
} // namespace Chronicle