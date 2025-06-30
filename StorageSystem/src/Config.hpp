#pragma once
#include "Util.hpp"
#include <memory>
#include <mutex>

namespace storage {
    const char *Config_File = "Storage.conf";

    class Config {
    private:
        int _m_server_port;
        std::string _m_server_ip;
        std::string _m_download_prefix;         // HTTP post download前缀
        std::string _m_deep_storage_dir;        // 压缩存储文件的存储路径
        std::string _m_low_storage_dir;         // 快速存储文件的存储路径
        std::string _m_storage_info;            // 已存储文件的记录文件
        int _m_bundle_format;                   // 压缩算法类型, 4表示BUNDLE_LZIP
    private:
        //static std::mutex _mtx;
        //static Config *_instance;   // 懒汉模式
        Config() {
            if (ReadConfig() == false) {
                Chronicle::GetLogger("asynclogger")->Fatal("ReadConfig failed");
                return;
            }
            Chronicle::GetLogger("asynclogger")->Info("ReadConfig complicate");
        }

    public:
        // 读取配置文件信息
        bool ReadConfig() {
            Chronicle::GetLogger("asynclogger")->Info("ReadConfig start");

            storage::FileUtil fu(Config_File);
            std::string content;
            if (!fu.GetContent(&content)) {
                return false;
            }

            Json::Value root;
            storage::JsonUtil::UnSerialize(content, &root);

            // 转换的时候需要加asint, asstring, 转换原有的json::value类型
            _m_server_port = root["server_port"].asInt();
            _m_server_ip = root["server_ip"].asString();
            _m_download_prefix = root["download_prefix"].asString();
            _m_storage_info = root["storage_info"].asString();
            _m_deep_storage_dir = root["deep_storage_dir"].asString();
            _m_low_storage_dir = root["low_storage_dir"].asString();
            _m_bundle_format = root["bundle_format"].asInt();
            
            return true;
        }

        int GetServerPort() {
            return _m_server_port;
        }

        std::string GetServerIp() {
            return _m_server_ip;
        }

        std::string GetDownloadPrefix() {
            return _m_download_prefix;
        }

        // 获取采用的压缩算法
        // 4: BUNDLE_LZIP
        int GetBundleFormat() {
            return _m_bundle_format;
        }

        std::string GetDeepStorageDir() {
            return _m_deep_storage_dir;
        }

        std::string GetLowStorageDir() {
            return _m_low_storage_dir;
        }

        // 返回已存储文件的记录文件路径
        std::string GetStorageInfoFile() {
            return _m_storage_info;
        }

    public:
        // // 获取单例类对象, 懒汉模式
        // static Config *GetInstance() {
        //     if (_instance == nullptr) {
        //         _mtx.lock();
        //         if (_instance == nullptr) {
        //             _instance = new Config();
        //         }
        //         _mtx.unlock();
        //     }
        //     return _instance;
        // }

        //单例模式, 懒汉, 保证线程安全(通过C++11局部static变量)
        static Config *GetInstance() {
            static Config instance;  // 局部静态变量，C++11保证线程安全
            return &instance;
        }
    };

    // // 静态成员类外初始化
    // std::mutex Config::_mtx;
    // Config *Config::_instance = nullptr;
}