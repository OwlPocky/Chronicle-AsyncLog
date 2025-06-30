#pragma once
#include "Config.hpp"
#include <unordered_map>
#include <pthread.h>

namespace storage{
    // 存储文件的元数据
    typedef struct StorageInfo{       
        time_t atime;               // 文件最后访问时间
        time_t mtime;               // 文件最后修改时间
        size_t fsize;               // 文件大小(KB)
        std::string storage_path;   // 文件在服务器上的存储路径, 如./low_storage/...
        std::string url;            // 文件的访问URL路径, /download/..., 前面拼接ip地址

        // 初始化文件信息, 从文件路径获取文件属性, 生成用于访问的URL
        // 每个文件都要初始化一次
        bool NewStorageInfo(const std::string &path) {
            Chronicle::GetLogger("asynclogger")->Info("NewStorageInfo start");
            FileUtil f(path);
            if (!f.Exists())
            {
                Chronicle::GetLogger("asynclogger")->Info("file not exists");
                return false;
            }

            atime = f.LastAccessTime();
            mtime = f.LastModifyTime();
            fsize = f.FileSize();
            storage_path = path;

            // 生成文件的访问URL(下载前缀./download/+文件名)
            storage::Config *config = storage::Config::GetInstance();
            url = config->GetDownloadPrefix() + f.FileName();
            cout << "url " << url << endl;
            cout << "storage_path " << storage_path << endl;

            // 生成格式化的时间字符串
            char mtimebuf[32], atimebuf[32];
            struct tm* tm_mtime = localtime(&mtime);
            struct tm* tm_atime = localtime(&atime);
            strftime(mtimebuf, sizeof(mtimebuf), "%Y-%m-%d %H:%M:%S", tm_mtime);
            strftime(atimebuf, sizeof(atimebuf), "%Y-%m-%d %H:%M:%S", tm_atime);

            Chronicle::GetLogger("asynclogger")->Info("download_url:%s, mtime:%s, atime:%s, fsize:%d", url.c_str(), mtimebuf, atimebuf, fsize);
            Chronicle::GetLogger("asynclogger")->Info("NewStorageInfo end");
            return true;
        }
    } StorageInfo; // struct StorageInfo

    // 负责文件元数据的存储、查询
    class DataManager {
    private:
        std::string                                     _m_storage_file;    // 存储文件的元数据文件名
        pthread_rwlock_t                                _m_rwlock;          // 读写锁
        std::unordered_map<std::string, StorageInfo>    _m_table;           // (文件url /download/..., StorageInfo)
        bool                                            _m_need_persist;    // 是否持久化数据到硬盘, 读取元数据时为false

    public:
        DataManager() {
            Chronicle::GetLogger("asynclogger")->Info("DataManager construct start");
            _m_storage_file = storage::Config::GetInstance()->GetStorageInfoFile();
            pthread_rwlock_init(&_m_rwlock, NULL);
            _m_need_persist = false;    // 初始化的文件是已经存储的, 不需要持久化到硬盘
            InitLoad();     // 从元数据文件加载已存储的文件数据
            _m_need_persist = true;
            Chronicle::GetLogger("asynclogger")->Info("DataManager construct end");
        }

        ~DataManager() {
            pthread_rwlock_destroy(&_m_rwlock);
        }

        // 初始化程序运行时从元数据文件中读取已存储的文件数据, 保存到m_table内存
        bool InitLoad() {
            Chronicle::GetLogger("asynclogger")->Info("init datamanager");
            storage::FileUtil f(_m_storage_file);
            if (!f.Exists()){
                Chronicle::GetLogger("asynclogger")->Info("there is no storage file info need to load");
                return true;
            }

            std::string body;
            if (!f.GetContent(&body)){
                return false;
            }
                
            // 反序列化
            Json::Value root;
            storage::JsonUtil::UnSerialize(body, &root);
            // 将反序列化得到的Json::Value中的数据添加到table中
            for (int i = 0; i < root.size(); i++) {
                StorageInfo info;
                info.fsize = root[i]["fsize"].asInt();
                info.atime = root[i]["atime"].asInt();
                info.mtime = root[i]["mtime"].asInt();
                info.storage_path = root[i]["storage_path"].asString();
                info.url = root[i]["url"].asString();
                Insert(info);   // 已保存文件保存到m_table
            }
            return true;
        }

        // 将内存中的元数据持久化到硬盘
        // 每次有信息改变则需要持久化存储一次, update、insert触发
        bool Storage() { 
        // 把table中的数据转成json格式存入文件
            Chronicle::GetLogger("asynclogger")->Info("message storage start");
            std::vector<StorageInfo> arr;
            // 读取所有文件的元数据StorageInfo
            if (!GetAll(&arr)) {
                Chronicle::GetLogger("asynclogger")->Warn("GetAll fail, can't get StorageInfo");
                return false;
            }

            Json::Value root;
            for (auto e: arr) {
                Json::Value item;
                item["mtime"] = (Json::Int64)e.mtime;
                item["atime"] = (Json::Int64)e.atime;
                item["fsize"] = (Json::Int64)e.fsize;
                item["url"] = e.url.c_str();
                item["storage_path"] = e.storage_path.c_str();
                root.append(item);
            }

            // 序列化
            std::string body;
            Chronicle::GetLogger("asynclogger")->Info("new message for StorageInfo:%s", body.c_str());
            JsonUtil::Serialize(root, &body);

            // 写入元数据文件
            FileUtil f(_m_storage_file);
            
            if (f.SetContent(body.c_str(), body.size()) == false){
                Chronicle::GetLogger("asynclogger")->Error("SetContent for StorageInfo Error");
            }

            Chronicle::GetLogger("asynclogger")->Info("message storage end");
            return true;
        }

        // 插入文件元数据到m_table, 并持久化到硬盘
        bool Insert(const StorageInfo &info) {
            Chronicle::GetLogger("asynclogger")->Info("data_message Insert start");
            pthread_rwlock_wrlock(&_m_rwlock);  // 写锁
            _m_table[info.url] = info;
            pthread_rwlock_unlock(&_m_rwlock);
            // 持久化到硬盘, 由于初始化时会调用, 所以多了是否持久化的判断
            if (_m_need_persist == true && Storage() == false) {
                Chronicle::GetLogger("asynclogger")->Error("data_message Insert:Storage Error");
                return false;
            }
            Chronicle::GetLogger("asynclogger")->Info("data_message Insert end");
            return true;
        }

        // 更新文件元数据
        bool Update(const StorageInfo &info) {
            Chronicle::GetLogger("asynclogger")->Info("data_message Update start");
            pthread_rwlock_wrlock(&_m_rwlock);
            _m_table[info.url] = info;
            pthread_rwlock_unlock(&_m_rwlock);
            // 持久化到硬盘
            if (Storage() == false) {
                Chronicle::GetLogger("asynclogger")->Error("data_message Update:Storage Error");
                return false;
            }
            Chronicle::GetLogger("asynclogger")->Info("data_message Update end");
            return true;
        }

        // 根据URL查询文件元数据StorageInfo
        bool GetOneByURL(const std::string &key, StorageInfo *info) {
            pthread_rwlock_rdlock(&_m_rwlock);
            // URL是key，调用find()
            if (_m_table.find(key) == _m_table.end()) {
                pthread_rwlock_unlock(&_m_rwlock);
                return false;
            }
            *info = _m_table[key]; // 获取url对应的文件存储信息
            pthread_rwlock_unlock(&_m_rwlock);
            return true;
        }

        // 根据存储服务器上的文件存储路径查询文件元数据StorageInfo
        bool GetOneByStoragePath(const std::string &storage_path, StorageInfo *info) {
            pthread_rwlock_rdlock(&_m_rwlock);
            for (auto e: _m_table) {
                if (e.second.storage_path == storage_path) {
                    *info = e.second;
                    pthread_rwlock_unlock(&_m_rwlock);
                    return true;
                }
            }
            pthread_rwlock_unlock(&_m_rwlock);
            return false;
        }

        // 从内存中获取所有文件的元数据, 只获取StorageInfo
        bool GetAll(std::vector<StorageInfo> *arry) {
            pthread_rwlock_rdlock(&_m_rwlock);
            for (auto e: _m_table){
                arry->emplace_back(e.second);
            }
            pthread_rwlock_unlock(&_m_rwlock);
            return true;
        }
    }; // namespace DataManager
}