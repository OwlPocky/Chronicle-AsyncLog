#pragma once
#include "DataManager.hpp"

#include <sys/queue.h>
#include <event.h>
// for http
#include <evhttp.h>
#include <event2/http.h>

#include <fcntl.h>
#include <sys/stat.h>

#include <regex>

#include "base64.h"

extern storage::DataManager *data_mgr;
namespace storage {

    // 基于Libevent实现的HTTP文件存储服务器
    class Service {
    public:
        Service() {
#ifdef DEBUG_LOG
            Chronicle::GetLogger("asynclogger")->Debug("Service start(Construct)");
#endif
            // 存储服务器8086相关配置
            _m_server_port = Config::GetInstance()->GetServerPort();
            _m_server_ip = Config::GetInstance()->GetServerIp();
            _m_download_prefix = Config::GetInstance()->GetDownloadPrefix();
#ifdef DEBUG_LOG
            Chronicle::GetLogger("asynclogger")->Debug("Service end(Construct)");
#endif
        }

        // 启动HTTP服务器
        bool Run() {
            // 1. 初始化libevent事件基
            event_base *base = event_base_new();
            if (base == NULL) {
                Chronicle::GetLogger("asynclogger")->Fatal("event_base_new err!");
                return false;
            }

            // 2. 配置监听地址和端口
            sockaddr_in sin;
            memset(&sin, 0, sizeof(sin));
            sin.sin_family = AF_INET;
            sin.sin_port = htons(_m_server_port);
            // 3. 创建HTTP服务器实例
            evhttp *httpd = evhttp_new(base);
            // 4. 绑定存储服务器ip和端口, 0.0.0.0监听所有网卡
            cout << "Run() evhttp_bind_socket 0.0.0.0:" << _m_server_port << endl;
            if (evhttp_bind_socket(httpd, "0.0.0.0", _m_server_port) != 0) {
                Chronicle::GetLogger("asynclogger")->Fatal("evhttp_bind_socket failed!");
                return false;
            }
            // 5. 设定回调函数(通用generic callback, 也可以为特定的URI指定callback)
            evhttp_set_gencb(httpd, GenHandler, NULL);

            // 6. 事件循环: 处理客户端请求
            if (base) {
#ifdef DEBUG_LOG
                Chronicle::GetLogger("asynclogger")->Debug("event_base_dispatch");
#endif
                // 阻塞, 如果有客户端请求, 执行回调函数GenHandler
                if (event_base_dispatch(base) == -1) {
                    Chronicle::GetLogger("asynclogger")->Debug("event_base_dispatch err");
                }
            }
            // 7. 释放资源
            if (base){
                event_base_free(base);
            }
            if (httpd){
                evhttp_free(httpd);
            }
            return true;
        }

    private:
        // 客户端发数据到存储服务器ip:端口
        uint16_t _m_server_port;            // 存储服务器监听端口
        std::string _m_server_ip;           // 存储服务器ip
        std::string _m_download_prefix;     // 文件下载URL前缀, /download/...

    private:
        // libevent回调函数(通用)
        static void GenHandler(struct evhttp_request *req, void *arg) {
            // 解析URI路径
            std::string path = evhttp_uri_get_path(evhttp_request_get_evhttp_uri(req));
            path = UrlDecode(path);
            cout << "GenHandler() path " << path << endl;
            Chronicle::GetLogger("asynclogger")->Info("get req, uri: %s", path.c_str());

            // 下载请求
            if (path.find("/download/") != std::string::npos) {
                Download(req, arg);
            }
            // 上传请求
            else if (path == "/upload") {
                Upload(req, arg);
            }
            // 根路径请求, 显示已存储文件列表, 返回一个html页面给浏览器
            else if (path == "/") {
                ListShow(req, arg);
            }
            // 未匹配, 返回404
            else {
                evhttp_send_reply(req, HTTP_NOTFOUND, "Not Found", NULL);
            }
        }

        // 文件上传
        static void Upload(struct evhttp_request *req, void *arg) {
            Chronicle::GetLogger("asynclogger")->Info("Upload start");
            // 1. 获取HTTP请求体内容
            struct evbuffer *buf = evhttp_request_get_input_buffer(req);
            if (buf == nullptr) {
                Chronicle::GetLogger("asynclogger")->Info("evhttp_request_get_input_buffer is empty");
                return;
            }

            // 2. 获取请求体长度
            size_t len = evbuffer_get_length(buf);
            Chronicle::GetLogger("asynclogger")->Info("evbuffer_get_length is %u", len);
            if (len == 0) {
                evhttp_send_reply(req, HTTP_BADREQUEST, "file empty", NULL);    //客户端错误400
                Chronicle::GetLogger("asynclogger")->Info("request body is empty");
                return;
            }

            // 3. 获取请求体内容
            std::string content(len, 0);
            if (evbuffer_copyout(buf, (void *)content.c_str(), len) == -1) {
                Chronicle::GetLogger("asynclogger")->Error("evbuffer_copyout error");
                evhttp_send_reply(req, HTTP_INTERNAL, NULL, NULL);
                return;
            }

            // 4. 从请求头获取文件名
            std::string filename = evhttp_find_header(req->input_headers, "FileName");
            // 解码文件名, 客户端base64编码
            filename = base64_decode(filename);

            // 5. 获取存储类型, 客户端自定义请求头StorageType
            std::string storage_type = evhttp_find_header(req->input_headers, "StorageType");
            // 存储服务器存储路径
            std::string storage_path;
            // 快速存储, 不压缩
            if (storage_type == "low") {
                storage_path = Config::GetInstance()->GetLowStorageDir();
            }
            // 深度存储, 压缩
            else if (storage_type == "deep") {
                storage_path = Config::GetInstance()->GetDeepStorageDir();
            }
            // 未匹配
            else {
                Chronicle::GetLogger("asynclogger")->Info("evhttp_send_reply: HTTP_BADREQUEST");
                evhttp_send_reply(req, HTTP_BADREQUEST, "Illegal storage type", NULL);
                return;
            }

            // 6. 如果目录不存在, 创建
            FileUtil dirCreate(storage_path);
            dirCreate.CreateDirectory();

            // 7. 存储服务器的完整文件路径
            storage_path += filename;
#ifdef DEBUG_LOG
            Chronicle::GetLogger("asynclogger")->Debug("storage_path:%s", storage_path.c_str());
#endif

            // 8. 根据不同的存储方案决定是否压缩
            FileUtil fu(storage_path);
            if (storage_path.find("low_storage") != std::string::npos) {
                if (fu.SetContent(content.c_str(), len) == false) {
                    Chronicle::GetLogger("asynclogger")->Error("low_storage fail, evhttp_send_reply: HTTP_INTERNAL");
                    evhttp_send_reply(req, HTTP_INTERNAL, "server error", NULL);    // 内部错误500
                    return;
                }
                else {
                    Chronicle::GetLogger("asynclogger")->Info("low_storage success");
                }
            }
            else {
                if (fu.Compress(content, Config::GetInstance()->GetBundleFormat()) == false) {
                    Chronicle::GetLogger("asynclogger")->Error("deep_storage fail, evhttp_send_reply: HTTP_INTERNAL");
                    evhttp_send_reply(req, HTTP_INTERNAL, "server error", NULL);
                    return;
                }
                else {
                    Chronicle::GetLogger("asynclogger")->Info("deep_storage success");
                }
            }

            // 9. 记录文件元数据, 添加到元数据文件
            StorageInfo info;
            info.NewStorageInfo(storage_path);  // 更新新存储的文件info信息
            data_mgr->Insert(info);             // 将info插入到data_mgr

            // 10. 返回200 ok
            evhttp_send_reply(req, HTTP_OK, "Success", NULL);
            Chronicle::GetLogger("asynclogger")->Info("upload finish:success");
        }

        // 返回时间戳字符串
        static std::string TimetoStr(time_t t) {
            std::string tmp = std::ctime(&t);
            return tmp;
        }

        // 生成文件列表HTML
        static std::string generateModernFileList(const std::vector<StorageInfo> &files) {
            std::stringstream ss;
            ss << "<div class='file-list'><h3>已上传文件</h3>";

            // 遍历所有文件元数据的files, 作为HTML列表项
            for (const auto &file : files) {
                std::string filename = FileUtil(file.storage_path).FileName();

                // 从路径中解析存储类型
                std::string storage_type;
                if (file.storage_path.find("deep") != std::string::npos) {
                    storage_type = "deep";
                }
                else {
                    storage_type = "low";
                }

                // 构建HTML文件列表项
                ss << "<div class='file-item'>"
                   << "<div class='file-info'>"
                   << "<span>📄" << filename << "</span>"
                   << "<span class='file-type'>"
                   << (storage_type == "deep" ? "压缩存储" : "快速存储")
                   << "</span>"
                   << "<span>" << formatSize(file.fsize) << "</span>"
                   << "<span>" << TimetoStr(file.mtime) << "</span>"
                   << "</div>"
                   << "<button onclick=\"window.location='" << file.url << "'\">⬇️ 下载</button>"
                   << "</div>";
            }

            ss << "</div>";
            return ss.str();
        }

        // 文件大小格式化
        static std::string formatSize(uint64_t bytes) {
            const char *units[] = {"B", "KB", "MB", "GB"};
            int unit_index = 0;
            double size = bytes;

            while (size >= 1024 && unit_index < 3)
            {
                size /= 1024;
                unit_index++;
            }

            std::stringstream ss;
            ss << std::fixed << std::setprecision(2) << size << " " << units[unit_index];
            return ss.str();
        }

        // 文件列表展示, 只要不是upload和download, 就展示文件列表
        static void ListShow(struct evhttp_request *req, void *arg) {
            Chronicle::GetLogger("asynclogger")->Info("ListShow()");
            // 1. 获取所有的文件存储信息
            std::vector<StorageInfo> arry;
            data_mgr->GetAll(&arry);

            // 2. 读取HTML模板文件
            std::ifstream templateFile("index.html");
            std::string templateContent(
                (std::istreambuf_iterator<char>(templateFile)),
                std::istreambuf_iterator<char>());

            // 3. 替换html文件中的文件列表占位符
            templateContent = std::regex_replace(templateContent,
                                                 std::regex("\\{\\{FILE_LIST\\}\\}"),
                                                 generateModernFileList(arry));
            // 4. 替换html文件中服务器地址占位符
            templateContent = std::regex_replace(templateContent,
                                                 std::regex("\\{\\{BACKEND_URL\\}\\}"),
                                                "http://"+storage::Config::GetInstance()->GetServerIp()+":"+std::to_string(storage::Config::GetInstance()->GetServerPort()));
            // 5. 获取请求的输出evbuffer, 修改evbuffer消息体
            struct evbuffer *buf = evhttp_request_get_output_buffer(req);
            auto response_body = templateContent;
            evbuffer_add(buf, (const void *)response_body.c_str(), response_body.size());
            // 6. 设置响应头
            evhttp_add_header(req->output_headers, "Content-Type", "text/html;charset=utf-8");
            // 7. 发送HTTP响应包
            evhttp_send_reply(req, HTTP_OK, NULL, NULL);
            Chronicle::GetLogger("asynclogger")->Info("ListShow() finish");
        }

        // 生成文件ETag: filename-fsize-mtime
        // 断点续传: 标识资源版本信息, 实现缓存验证
        static std::string GetETag(const StorageInfo &info) {
            FileUtil fu(info.storage_path);
            std::string etag = fu.FileName();
            etag += "-";
            etag += std::to_string(info.fsize);
            etag += "-";
            etag += std::to_string(info.mtime);
            return etag;
        }

        // 下载文件
        static void Download_bak(struct evhttp_request *req, void *arg) {
            // 1. 解析请求路径, 获取文件元数据StorageInfo, 并获得实际存储路径
            StorageInfo info;
            std::string resource_path = evhttp_uri_get_path(evhttp_request_get_evhttp_uri(req));
            resource_path = UrlDecode(resource_path);
            // 根据URL查询文件元数据StorageInfo
            data_mgr->GetOneByURL(resource_path, &info);
            Chronicle::GetLogger("asynclogger")->Info("request resource_path:%s", resource_path.c_str());

            // 找到文件实际的存储路径
            std::string download_path = info.storage_path;
            Chronicle::GetLogger("asynclogger")->Info("request download_path:%s", download_path.c_str());

            // 2. 根据文件存储方式, 决定是否解压缩
            // 深度存储, 将文件压缩到快速存储路径下, 再提供下载
            if (info.storage_path.find(Config::GetInstance()->GetLowStorageDir()) == std::string::npos) {
                Chronicle::GetLogger("asynclogger")->Info("uncompressing:%s", info.storage_path.c_str());
                FileUtil fu(info.storage_path);
                // 更新前缀为快速存储./low_storage/, 后接文件名
                download_path = Config::GetInstance()->GetLowStorageDir() +
                                std::string(download_path.begin() + download_path.find_last_of('/') + 1, download_path.end());
                FileUtil dirCreate(Config::GetInstance()->GetLowStorageDir());
                dirCreate.CreateDirectory();
                // 将文件解压缩到./low_storage/下(这里逻辑不是很合适, 文件名冲突会出问题)
                fu.UnCompress(download_path);
            }

            // 3. 处理文件不存在的异常情况
            FileUtil fu(download_path);
            // 文件不存在, 深度存储, 代表压缩中出现错误, 返回内部错误500
            if (fu.Exists() == false && info.storage_path.find("deep_storage") != std::string::npos) {
                Chronicle::GetLogger("asynclogger")->Info("evhttp_send_reply: 500 - UnCompress failed");
                evhttp_send_reply(req, HTTP_INTERNAL, NULL, NULL);
            }
            // 文件不存在, 快速存储, 客户端错误400
            else if (fu.Exists() == false && info.storage_path.find("low_storage") == std::string::npos) {
                Chronicle::GetLogger("asynclogger")->Info("evhttp_send_reply: 400 - bad request,file not exists");
                evhttp_send_reply(req, HTTP_BADREQUEST, "file not exists", NULL);
            }

            // 4. 检查是否符合文件断点续传条件
            // 首先验证ETag资源版本, 如果版本一致, 才允许断点续传
            bool retrans = false;   // 默认不支持断点续传
            std::string old_etag;   // 客户端上次过去的文件ETag
            auto if_range = evhttp_find_header(req->input_headers, "If-Range");     //从请求头中获取if-range字段
            if (if_range != NULL) {
                old_etag = if_range;
                // If-Range字段生效, 且值与最新etag一致, 允许断点续传
                if (old_etag == GetETag(info)) {
                    retrans = true;
                    Chronicle::GetLogger("asynclogger")->Info("%s need breakpoint continuous transmission", download_path.c_str());
                }
            }

            // 5. 读取文件数据, 将数据放入响应体
            // 文件不存在, 返回404
            if (fu.Exists() == false) {
                Chronicle::GetLogger("asynclogger")->Info("%s not exists", download_path.c_str());
                download_path += "not exists";
                evhttp_send_reply(req, 404, download_path.c_str(), NULL);
                return;
            }
            // 获取响应缓冲区, 并添加文件内容
            evbuffer *outbuf = evhttp_request_get_output_buffer(req);
            int fd = open(download_path.c_str(), O_RDONLY);
            if (fd == -1) {
                Chronicle::GetLogger("asynclogger")->Error("open file error: %s -- %s", download_path.c_str(), strerror(errno));
                evhttp_send_reply(req, HTTP_INTERNAL, strerror(errno), NULL);
                return;
            }
            // 零拷贝, 将文件内容添加到响应体
            if (evbuffer_add_file(outbuf, fd, 0, fu.FileSize()) == -1) {
                Chronicle::GetLogger("asynclogger")->Error("evbuffer_add_file: %d -- %s -- %s", fd, download_path.c_str(), strerror(errno));
            }
            // 6. 设置HTTP响应头部字段: ETag,  Accept-Ranges: bytes(用于支持断点续传)
            evhttp_add_header(req->output_headers, "Accept-Ranges", "bytes");           // 服务器声明, 通过Range指定续传字节位置
            evhttp_add_header(req->output_headers, "ETag", GetETag(info).c_str());      // ETag版本标识
            evhttp_add_header(req->output_headers, "Content-Type", "application/octet-stream");     // 二进制数据流, 浏览器可触发文件下载, 不直接渲染
            // 7. 根据断点续传状态返回消息体
            if (retrans == false) {
                evhttp_send_reply(req, HTTP_OK, "Success", NULL);
                Chronicle::GetLogger("asynclogger")->Info("evhttp_send_reply: HTTP_OK");
            }
            else {
                evhttp_send_reply(req, 206, "breakpoint continuous transmission", NULL);    // 区间请求响应的是206
                Chronicle::GetLogger("asynclogger")->Info("evhttp_send_reply: 206");
            }

            // 8. 清理解压缩产生的临时文件
            if (download_path != info.storage_path) {
                remove(download_path.c_str());
            }
        }

        // 下载文件
        static void Download(struct evhttp_request *req, void *arg) {
            // 1. 解析请求路径, 获取文件元数据StorageInfo, 并获得实际存储路径
            StorageInfo info;
            std::string resource_path = evhttp_uri_get_path(evhttp_request_get_evhttp_uri(req));
            resource_path = UrlDecode(resource_path);
            // 根据URL查询文件元数据StorageInfo
            data_mgr->GetOneByURL(resource_path, &info);
            Chronicle::GetLogger("asynclogger")->Info("request resource_path:%s", resource_path.c_str());

            // 找到文件实际的存储路径
            std::string download_path = info.storage_path;
            Chronicle::GetLogger("asynclogger")->Info("request download_path:%s", download_path.c_str());

            // 2. 根据文件存储方式, 决定是否解压缩
            // 深度存储, 将文件压缩到快速存储路径下, 再提供下载
            if (info.storage_path.find(Config::GetInstance()->GetLowStorageDir()) == std::string::npos) {
                Chronicle::GetLogger("asynclogger")->Info("uncompressing:%s", info.storage_path.c_str());
                FileUtil fu(info.storage_path);
                // 更新前缀为快速存储./low_storage/, 后接文件名
                download_path = Config::GetInstance()->GetLowStorageDir() +
                                std::string(download_path.begin() + download_path.find_last_of('/') + 1, download_path.end());
                FileUtil dirCreate(Config::GetInstance()->GetLowStorageDir());
                dirCreate.CreateDirectory();
                // 将文件解压缩到./low_storage/下(这里逻辑不是很合适, 文件名冲突会出问题)
                fu.UnCompress(download_path);
            }

            // 3. 处理文件不存在的异常情况
            FileUtil fu(download_path);
            // 文件不存在, 深度存储, 代表压缩中出现错误, 返回内部错误500
            if (fu.Exists() == false && info.storage_path.find("deep_storage") != std::string::npos) {
                Chronicle::GetLogger("asynclogger")->Info("evhttp_send_reply: 500 - UnCompress failed");
                evhttp_send_reply(req, HTTP_INTERNAL, NULL, NULL);
            }
            // 文件不存在, 快速存储, 客户端错误400
            else if (fu.Exists() == false && info.storage_path.find("low_storage") == std::string::npos) {
                Chronicle::GetLogger("asynclogger")->Info("evhttp_send_reply: 400 - bad request,file not exists");
                evhttp_send_reply(req, HTTP_BADREQUEST, "file not exists", NULL);
            }

            // 4. 检查是否符合文件断点续传条件
            // 首先验证ETag资源版本, 如果版本一致, 才允许断点续传
            bool retrans = false;   // 默认不支持断点续传
            std::string old_etag;   // 客户端上次过去的文件ETag
            auto if_range = evhttp_find_header(req->input_headers, "If-Range");     //从请求头中获取if-range字段
            if (if_range != NULL) {
                old_etag = if_range;
                // If-Range字段生效, 且值与最新etag一致, 允许断点续传
                if (old_etag == GetETag(info)) {
                    retrans = true;
                    Chronicle::GetLogger("asynclogger")->Info("%s need breakpoint continuous transmission", download_path.c_str());
                }
            }

            // 5. 读取文件数据, 将数据放入响应体
            // 文件不存在, 返回404
            if (fu.Exists() == false) {
                Chronicle::GetLogger("asynclogger")->Info("%s not exists", download_path.c_str());
                download_path += "not exists";
                evhttp_send_reply(req, 404, download_path.c_str(), NULL);
                return;
            }
            // 获取响应缓冲区, 并添加文件内容
            evbuffer *outbuf = evhttp_request_get_output_buffer(req);
            int fd = open(download_path.c_str(), O_RDONLY);
            if (fd == -1) {
                Chronicle::GetLogger("asynclogger")->Error("open file error: %s -- %s", download_path.c_str(), strerror(errno));
                evhttp_send_reply(req, HTTP_INTERNAL, strerror(errno), NULL);
                return;
            }

            // a. 解析Range请求头, 提取起始和结束位置
            off_t start_offset = 0;                 // 起始偏移量(默认从0开始)
            off_t end_offset = fu.FileSize() - 1;   // 结束偏移量(默认到文件末尾)
            bool has_valid_range = false;           // 是否有有效的Range头

            // 只有支持断点续传且有Range头时才解析
            if (retrans) {
                // 从请求头中获取Range字段("bytes=start-end", 如"bytes=5000-" 或 "bytes=5000-9999")
                const char *range_header = evhttp_find_header(req->input_headers, "Range");
                cout << "   retrans range_header " << range_header << endl;
                if (range_header) {
                    // 验证Range头是否以"bytes="开头
                    if (strncmp(range_header, "bytes=", 6) == 0) {
                        // 将const char*转换为char*(允许临时修改字符串进行解析)
                        // 注：libevent允许修改请求头字段, const_cast在此场景下是安全的
                        char *mutable_range = const_cast<char*>(range_header);
                        // 查找"-"分隔符, 用于分割start和end位置
                        char *dash = strchr(mutable_range + 6, '-');
                        if (dash) {
                            *dash = '\0';  // 截断字符串, 便于转换
                            start_offset = atoll(range_header + 6);  // 解析起始位置, 如"bytes=5000-"中的5000
                            *dash = '-';  // 恢复原字符串

                            // 处理Range头的结束位置(分两种情况)
                            if (dash[1] != '\0') {
                                // 情况1：包含结束位置(如"bytes=5000-9999")
                                end_offset = atoll(dash + 1);
                            } else {
                                // 情况2：未包含结束位置(如"bytes=5000-"), 表示从start到文件末尾
                                end_offset = fu.FileSize() - 1;
                            }

                            cout << "   start_offset " << start_offset << endl;
                            cout << "   end_offset " << end_offset << endl;
                            cout << "   FileSize" << fu.FileSize() << endl;

                            // 验证Range是否有效(起始位置不能超过文件大小)
                            if (start_offset < fu.FileSize()) {
                                has_valid_range = true;
                                Chronicle::GetLogger("asynclogger")->Info("Range: bytes %ld-%ld/%ld", 
                                                                          start_offset, end_offset, fu.FileSize());
                            } else {
                                // Range无效(如起始位置超过文件大小), 返回416错误
                                Chronicle::GetLogger("asynclogger")->Info("Invalid Range: bytes %ld-%ld/%ld", 
                                                                          start_offset, end_offset, fu.FileSize());
                                evhttp_add_header(req->output_headers, "Content-Range", 
                                                ("bytes */" + std::to_string(fu.FileSize())).c_str());
                                evhttp_send_reply(req, 416, "Range Not Satisfiable", NULL);
                                close(fd);  // 关闭文件描述符
                                return;
                            }
                        }
                    }
                }
            }

            // b. 根据Range头调整文件读取参数
            off_t read_length = end_offset - start_offset + 1;  // 实际读取长度

            // 零拷贝, 将文件内容添加到响应体
            // 使用调整后的偏移量和长度
            if (evbuffer_add_file(outbuf, fd, start_offset, read_length) == -1) {
                Chronicle::GetLogger("asynclogger")->Error("evbuffer_add_file: %d -- %s -- %s", fd, download_path.c_str(), strerror(errno));
            }

            // 6. 设置HTTP响应头部字段: ETag,  Accept-Ranges: bytes(用于支持断点续传)
            evhttp_add_header(req->output_headers, "Accept-Ranges", "bytes");           // 服务器声明, 通过Range指定续传字节位置
            evhttp_add_header(req->output_headers, "ETag", GetETag(info).c_str());      // ETag版本标识
            evhttp_add_header(req->output_headers, "Content-Type", "application/octet-stream");     // 二进制数据流, 浏览器可触发文件下载, 不直接渲染

            // c. 添加Content-Range头(断点续传)
            if (has_valid_range) {
                char content_range[128];
                snprintf(content_range, sizeof(content_range), "bytes %ld-%ld/%ld",
                         start_offset, end_offset, fu.FileSize());
                evhttp_add_header(req->output_headers, "Content-Range", content_range);
                cout << "   content-range " << content_range << endl;
                Chronicle::GetLogger("asynclogger")->Info("Content-Range: %s", content_range);
            }

            // 7. 根据断点续传状态返回消息体
            if (retrans == false || !has_valid_range) {
                // 不支持断点续传或Range无效, 返回完整文件(200 OK)
                evhttp_send_reply(req, HTTP_OK, "Success", NULL);
                Chronicle::GetLogger("asynclogger")->Info("evhttp_send_reply: HTTP_OK");
            }
            else {
                // 支持断点续传且Range有效, 返回部分内容(206 Partial Content)
                evhttp_send_reply(req, 206, "breakpoint continuous transmission", NULL);
                Chronicle::GetLogger("asynclogger")->Info("evhttp_send_reply: 206");
            }

            // 8. 清理解压缩产生的临时文件
            if (download_path != info.storage_path) {
                remove(download_path.c_str());
            }
        }
    };
}
