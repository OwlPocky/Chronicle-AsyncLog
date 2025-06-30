#pragma once
#include <sys/stat.h>
#include <sys/types.h>
#include <jsoncpp/json/json.h>

#include <ctime>
#include <fstream>
#include <iostream>

#include <sstream>

using std::cout;
using std::endl;
namespace Chronicle {
    namespace Util {
        class Date {
        public:
            static time_t Now() { return time(nullptr); }
        };
        
        class File {
        public:
            //文件是否存在
            static bool Exists(const std::string &filename) {
                struct stat st;
                return (0 == stat(filename.c_str(), &st));
            }
            //返回文件所在目录
            static std::string Path(const std::string &filename) {
                if (filename.empty()){
                    return "";
                }
                std::string::size_type pos = filename.find_last_of("/\\");
                if (pos != std::string::npos){
                    cout << "Util::Path() " << filename.substr(0, pos + 1) << endl; //./logfile/
                    return filename.substr(0, pos + 1); //substr第二个参数是截取长度, 截取到pos('/'或'\')为止
                }
                return "";
            }

            // 递归创建多级目录
            static void CreateDirectory(const std::string &pathname) {
                std::cout << "Util::CreateDirectory() " << pathname << std::endl;
                if (pathname.empty()) {
                    perror("文件所给路径为空：");
                    return;
                }

                // 处理绝对路径（以 '/' 开头）和相对路径
                std::string normalizedPath = pathname;
                if (!normalizedPath.empty() && normalizedPath.back() != '/' && normalizedPath.back() != '\\') {
                    normalizedPath += '/'; // 确保路径以分隔符结尾，方便分割
                }

                size_t start = 0;
                size_t pos = 0;
                //const size_t size = normalizedPath.size();

                // 逐级创建目录（a/b/c/, 会依次创建a/、a/b/、a/b/c/）
                while ((pos = normalizedPath.find_first_of("/\\", start)) != std::string::npos) {
                    std::string subPath = normalizedPath.substr(0, pos);
                    if (subPath.empty()) { // 处理根目录（如 /a/b 中的第一个 '/'）
                        subPath = "/"; // 根目录特殊处理
                    }

                    // 跳过 "." 和 ".."
                    if (subPath == "." || subPath == "..") {
                        start = pos + 1;
                        continue;
                    }

                    // 创建目录（若不存在）
                    if (!Exists(subPath)) {
                        std::cout << "   mkdir " << subPath << std::endl;
                        if (mkdir(subPath.c_str(), 0755) != 0) { // 检查创建失败
                            perror("mkdir failed");
                            return;
                        }
                    }

                    start = pos + 1; // 移动到下一级目录起点
                }
            }

            // 返回文件大小
            int64_t FileSize(std::string filename) {
                struct stat s;
                auto ret = stat(filename.c_str(), &s);
                if (ret == -1)
                {
                    perror("Get file size failed");
                    return -1;
                }
                return s.st_size;
            }

            // 获取文件内容
            bool GetContent(std::string *content, std::string filename) {
                // 打开文件
                std::ifstream ifs;
                ifs.open(filename.c_str(), std::ios::binary);
                if (ifs.is_open() == false)
                {
                    std::cout << "file open error" << std::endl;
                    return false;
                }

                // 读入content
                ifs.seekg(0, std::ios::beg); // 更改文件指针的偏移量
                size_t len = FileSize(filename);
                content->resize(len);
                ifs.read(&(*content)[0], len);
                if (!ifs.good())
                {
                    std::cout << __FILE__ << " " << __LINE__ << " read file content error" << std::endl;
                    ifs.close();
                    return false;
                }
                ifs.close();

                return true;
            }   
        }; // class file

        //Json序列化与反序列化
        class JsonUtil {
        public:
            //将Json::Value对象序列化为JSON格式字符串str
            static bool Serialize(const Json::Value &val, std::string *str) {
                Json::StreamWriterBuilder swb;
                std::unique_ptr<Json::StreamWriter> usw(swb.newStreamWriter());
                std::stringstream ss;
                if (usw->write(val, &ss) != 0)
                {
                    std::cout << "serialize error" << std::endl;
                    return false;
                }
                *str = ss.str();
                return true;
            }

            //将JSON格式字符串反序列化为Json::Value对象val
            static bool UnSerialize(const std::string &str, Json::Value *val) {
                Json::CharReaderBuilder crb;
                std::unique_ptr<Json::CharReader> ucr(crb.newCharReader());
                std::string err;
                if (ucr->parse(str.c_str(), str.c_str() + str.size(), val, &err) == false)
                {
                    std::cout << __FILE__ << " " << __LINE__ << " parse error" << err<<std::endl;
                    return false;
                }
                return true;
            }
        };  //class JsonUtil
        
        //单例
        //读取配置文件, 提供全局访问
        struct JsonData{
            static JsonData* GetJsonData(){
               static JsonData* json_data = new JsonData;
               return json_data;
            }
            private:
                JsonData(){
                    std::string content;
                    Chronicle::Util::File file;
                    if (file.GetContent(&content, "../../Chronicle/src/config.conf") == false){
                        std::cout << __FILE__ << " " << __LINE__ << " open config.conf failed" << std::endl;
                        perror(NULL);
                    }
                    Json::Value root;
                    Chronicle::Util::JsonUtil::UnSerialize(content, &root);
                    buffer_size = root["buffer_size"].asInt64();
                    threshold = root["threshold"].asInt64();
                    linear_growth = root["linear_growth"].asInt64();
                    flush_log = root["flush_log"].asInt64();
                    backup_addr = root["backup_addr"].asString();
                    backup_port = root["backup_port"].asInt();
                    thread_count = root["thread_count"].asInt();
                }
            public:
                size_t buffer_size;         // 缓冲区基础容量
                size_t threshold;           // 扩容方式阈值(超过该值采用线性增长, 否则指数增长)
                size_t linear_growth;       // 线性增长单次容量
                size_t flush_log;           // 控制日志同步到磁盘的时机，默认为0, 1调用fflush，2调用fsync
                std::string backup_addr;    // 日志备份服务器
                uint16_t backup_port;
                size_t thread_count;        // 线程池线程数量
        };
    } // namespace Util
} // namespace Chronicle
