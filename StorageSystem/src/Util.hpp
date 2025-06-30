#pragma once
#include "jsoncpp/json/json.h"
#include <cassert>
#include <sstream>
#include <memory>
#include "bundle.h"
#include "Config.hpp"
#include <iostream>
#include <experimental/filesystem>
#include <string>
#include <sys/stat.h>
#include <vector>
#include <fstream>
#include "../../Chronicle/src/Chronicle.hpp"

namespace storage {
    namespace fs = std::experimental::filesystem;

    /*
        URL编解码:
        编码: 字符 -> ASCII码 -> 转16进制(ToHex) -> 拼接%
        解码: 拆解% -> 转10进制(FromHex) -> 字符
    */
    static unsigned char ToHex(unsigned char x) {
        return x > 9 ? x + 55 : x + 48;
    }

    static unsigned char FromHex(unsigned char x) {
        unsigned char y;
        if (x >= 'A' && x <= 'Z') y = x - 'A' + 10;
        else if (x >= 'a' && x <= 'z') y = x - 'a' + 10;
        else if (x >= '0' && x <= '9') y = x - '0';
        else assert(0);
        return y;
    }
    
    static std::string UrlDecode(const std::string &str) {
        cout << "UrlDecode str = " << str << endl;
        std::string strTemp = "";
        size_t length = str.length();
        for (size_t i = 0; i < length; i++) {
            if (str[i] == '%') {
                assert(i + 2 < length);
                unsigned char high = FromHex((unsigned char)str[++i]);
                unsigned char low = FromHex((unsigned char)str[++i]);
                strTemp += high * 16 + low;
            }
            else{
                strTemp += str[i];
            }
        }
        cout << "   ret str " << strTemp << endl;
        return strTemp;
    }

    class FileUtil {
    private:
        std::string _m_filename;

    public:
        FileUtil(const std::string &filename) : _m_filename(filename) {}

        //  获取文件大小
        int64_t FileSize() {
            struct stat s;
            auto ret = stat(_m_filename.c_str(), &s);
            if (ret == -1) {
                Chronicle::GetLogger("asynclogger")->Info("%s, Get file size failed: %s", _m_filename.c_str(),strerror(errno));
                return -1;
            }
            return s.st_size;
        }

        // 获取文件最近访问时间
        time_t LastAccessTime() {
            struct stat s;
            auto ret = stat(_m_filename.c_str(), &s);
            if (ret == -1) {
                Chronicle::GetLogger("asynclogger")->Info("%s, Get file access time failed: %s", _m_filename.c_str(),strerror(errno));
                return -1;
            }
            return s.st_atime;
        }

        // 获取文件最近修改时间(Unix时间戳)
        time_t LastModifyTime() {
            struct stat s;
            auto ret = stat(_m_filename.c_str(), &s);
            if (ret == -1) {
                Chronicle::GetLogger("asynclogger")->Info("%s, Get file modify time failed: %s",_m_filename.c_str(), strerror(errno));
                return -1;
            }
            return s.st_mtime;
        }

        // 获取路径中的文件名
        std::string FileName() {
            auto pos = _m_filename.find_last_of("/");
            if (pos == std::string::npos){
                return _m_filename;
            }
            return _m_filename.substr(pos + 1, std::string::npos);
        }

        // 获取从文件pos处开始, len长度的数据
        bool GetPosLen(std::string *content, size_t pos, size_t len) {
            // 判断是否超出文件大小
            if (pos + len > FileSize()) {
                Chronicle::GetLogger("asynclogger")->Info("needed data larger than file size");
                return false;
            }

            // 打开文件(binary)
            std::ifstream ifs;
            ifs.open(_m_filename.c_str(), std::ios::binary);
            if (ifs.is_open() == false) {
                Chronicle::GetLogger("asynclogger")->Info("%s,file open error",_m_filename.c_str());
                return false;
            }

            // 读入content
            ifs.seekg(pos, std::ios::beg); // 更改文件指针的偏移量
            content->resize(len);
            ifs.read(&(*content)[0], len);
            if (!ifs.good()) {
                Chronicle::GetLogger("asynclogger")->Info("%s,read file content error",_m_filename.c_str());
                ifs.close();
                return false;
            }
            ifs.close();

            return true;
        }

        // 获取整个文件内容
        bool GetContent(std::string *content) {
            return GetPosLen(content, 0, FileSize());
        }

        // 写文件(覆盖原有内容)
        bool SetContent(const char *content, size_t len) {
            std::ofstream ofs;
            ofs.open(_m_filename.c_str(), std::ios::binary);
            if (!ofs.is_open()) {
                Chronicle::GetLogger("asynclogger")->Info("%s open error: %s", _m_filename.c_str(), strerror(errno));
                return false;
            }
            ofs.write(content, len);    // 写内核缓冲区
            if (!ofs.good()) {
                Chronicle::GetLogger("asynclogger")->Info("%s, file set content error",_m_filename.c_str());
                ofs.close();
            }
            ofs.close();    // 没有手动fsync(), close()触发落盘
            return true;
        }

        // 压缩文件
        bool Compress(const std::string &content, int format) {
            cout << "Util Compress: " << _m_filename << endl;
            std::string packed = bundle::pack(format, content);
            if (packed.size() == 0) {
                Chronicle::GetLogger("asynclogger")->Info("Compress packed size error:%d", packed.size());
                return false;
            }
            // 将压缩的数据写入压缩包文件中
            FileUtil f(_m_filename);
            if (f.SetContent(packed.c_str(), packed.size()) == false) {
                Chronicle::GetLogger("asynclogger")->Info("filename:%s, Compress SetContent error",_m_filename.c_str());
                return false;
            }
            return true;
        }

        // 解压缩, 输出到指定路径
        bool UnCompress(std::string &download_path) {
            cout << "Util UnCompress: " << download_path << endl;
            std::string body;
            if (this->GetContent(&body) == false) {
                Chronicle::GetLogger("asynclogger")->Info("filename:%s, uncompress get file content failed!",_m_filename.c_str());
                return false;
            }

            std::string unpacked = bundle::unpack(body);
            // 解压缩的数据写入新文件
            FileUtil fu(download_path);
            if (fu.SetContent(unpacked.c_str(), unpacked.size()) == false) {
                Chronicle::GetLogger("asynclogger")->Info("filename:%s, uncompress write packed data failed!",_m_filename.c_str());
                return false;
            }
            return true;
        }

        bool Exists() {
            return fs::exists(_m_filename);
        }

        bool CreateDirectory() {
            if (Exists()){
                return true;
            }
            return fs::create_directories(_m_filename);
        }

        // // 扫描目录下的所有文件(不包括子目录)
        // bool ScanDirectory(std::vector<std::string> *arry) {
        //     for (auto &p : fs::directory_iterator(_m_filename)) {
        //         if (fs::is_directory(p) == true){
        //             continue;
        //         }
        //         // 添加相对路径文件名
        //         arry->push_back(fs::path(p).relative_path().string());
        //     }
        //     return true;
        // }
    };

    /*JSON处理: 序列化和反序列化*/
    class JsonUtil {
    public:
        // 序列化: jsoncpp API将json::val转为json格式写入str
        static bool Serialize(const Json::Value &val, std::string *str) {
            Json::StreamWriterBuilder swb;
            swb["emitUTF8"] = true;
            std::unique_ptr<Json::StreamWriter> usw(swb.newStreamWriter());
            std::stringstream ss;
            if (usw->write(val, &ss) != 0) {
                Chronicle::GetLogger("asynclogger")->Info("serialize error");
                return false;
            }
            *str = ss.str();
            return true;
        }
        
        static bool UnSerialize(const std::string &str, Json::Value *val) {
            Json::CharReaderBuilder crb;
            std::unique_ptr<Json::CharReader> ucr(crb.newCharReader());
            std::string err;
            if (ucr->parse(str.c_str(), str.c_str() + str.size(), val, &err) == false) {
                Chronicle::GetLogger("asynclogger")->Info("parse error");
                return false;
            }
            return true;
        }
    };
}