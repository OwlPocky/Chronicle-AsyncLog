#pragma once
#include <iostream>
#include <string>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <pthread.h>
#include <mutex>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <functional>

using std::cout;
using std::endl;

using func_t = std::function<void(const std::string &)>;
const int backlog = 32;

/*
    用来接收备份日志的服务器, 该服务器可接收不同等级的log
*/
class TcpServer;

//多线程TCP服务器中，将客户端连接信息(如socket、IP地址)和服务器实例指针传递给工作线程
class ThreadData
{
public:
    ThreadData(int fd, const std::string &ip, const uint16_t &port, TcpServer *ts)
        : sock(fd), client_ip(ip), client_port(port), ts(ts){}

public:
    int sock;
    std::string client_ip;
    uint16_t client_port;
    TcpServer *ts;
};

class TcpServer
{
public:
    TcpServer(uint16_t port, func_t func)
        : _m_port(port), _m_func(func) {}
    void init_service(){
        // 创建
        _m_listen_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (_m_listen_sock == -1){
            std::cout << __FILE__ << " " << __LINE__ << " create socket error"<< strerror(errno)<< std::endl;
        }

        struct sockaddr_in local;
        local.sin_family = AF_INET;
        local.sin_port = htons(_m_port);
        local.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(_m_listen_sock, (struct sockaddr *)&local, sizeof(local)) < 0){
            std::cout << __FILE__ << " " << __LINE__ << " bind socket error"<< strerror(errno)<< std::endl;
        }

        if (listen(_m_listen_sock, backlog) < 0) {
            std::cout << __FILE__ << " " << __LINE__ << " listen error"<< strerror(errno)<< std::endl;
        }
    }

    static void *threadRoutine(void *args){
        pthread_detach(pthread_self()); // 防止在start_service处阻塞

        ThreadData *td = static_cast<ThreadData*>(args);
        std::string client_info = td->client_ip + ":" + std::to_string(td->client_port);
        td->ts->service(td->sock, move(client_info));
        close(td->sock);
        delete td;
        return nullptr;
    }

    void start_service(){
        while (true){
            struct sockaddr_in client_addr;
            socklen_t client_addrlen = sizeof(client_addr);
            int connfd = accept(_m_listen_sock, (struct sockaddr *)&client_addr, &client_addrlen);
            if (connfd < 0){
                std::cout << __FILE__ << " " << __LINE__ << " accept error"<< strerror(errno)<< std::endl;
                continue;
            }
            
            {
                std::string client_ip = inet_ntoa(client_addr.sin_addr);
                uint16_t client_port = ntohs(client_addr.sin_port);
                std::string client_info = client_ip + ":" + std::to_string(client_port);
                std::cout << "client connected: " << client_info << std::endl;
            }

            // 获取client端信息
            std::string client_ip = inet_ntoa(client_addr.sin_addr); // 网络序列转字符串
            uint16_t client_port = ntohs(client_addr.sin_port);

            // 多个线程提供服务
            // 传入线程数据类型来访问threadRoutine，因为该函数是static的，所以内部传入了data类型存了tcpserver类型
            pthread_t tid;
            ThreadData *td = new ThreadData(connfd, client_ip, client_port, this);
            pthread_create(&tid, nullptr, threadRoutine, td);
        }
    }

    void service(int sock, const std::string&& client_info) {
        char buf[1024];
        // 循环读取直到连接关闭
        while (true) { 
            ssize_t r_ret = read(sock, buf, sizeof(buf));
            if (r_ret == -1) {
                if (errno == EINTR) continue; // 处理被信号中断的情况
                std::cerr << "read error: " << strerror(errno) << std::endl;
                break;
            } else if (r_ret == 0) { // 客户端关闭连接
                std::cout << "client disconnected: " << client_info << std::endl;
                break;
            } else {
                buf[r_ret] = '\0';
                std::string tmp = buf;
                _m_func(client_info + tmp); // 处理数据, 这里是强制落盘
            }
        }
    }

    ~TcpServer() = default;

private:
    int _m_listen_sock;
    uint16_t _m_port;
    func_t _m_func;
};