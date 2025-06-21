#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>

#include "AsyncBuffer.hpp"

namespace Chronicle {
    //两种工作模式:
    //  async_safe: 安全模式, 固定大小缓冲区(缓冲区不增长), 写满阻塞生产者, buffer不扩容
    //  async_unsafe: 不安全模式, 动态扩容缓冲区(缓冲区增长), 写满不阻塞生产者, 可能会超出内吞
    enum class AsyncType { ASYNC_SAFE, ASYNC_UNSAFE };
    using CallBackFunc = std::function<void(Buffer&)>;
    //异步日志生产者消费者模型
    //  Push(): 多个外部任务会调用push, 向生产者写入
    //  ConsumerThreadEntry(): 唯一消费者线程, 负责处理双缓冲区swap以及调用读缓冲区回调函数
    //  Stop(): 结束该模型, 处理被阻塞的读写任务
    class AsyncWorker {
    public:
        using ptr = std::shared_ptr<AsyncWorker>;

        AsyncWorker(const CallBackFunc& cb, AsyncType async_type = AsyncType::ASYNC_SAFE):
            _m_async_type(async_type),
            _m_isStop(false),
            _m_thread(std::thread(&AsyncWorker::ConsumerThreadEntry, this)),
            _m_callback_func(cb) {}
        ~AsyncWorker() { Stop(); }
        AsyncWorker(const AsyncWorker&) = delete;
        AsyncWorker& operator=(const AsyncWorker&) = delete;

        //向生产者缓冲区写入数据
        void Push(const char* data, size_t len) {
            // 如果生产者队列不足以写下len长度数据，并且缓冲区是固定大小(SAFE mode)，那么阻塞
            std::unique_lock<std::mutex> lock(_m_mtx);
            if (_m_async_type == AsyncType::ASYNC_SAFE) {
                _m_cond_productor.wait(lock, [&]() {
                    // _m_isStop 或 可写容量足够 就继续运行
                    return _m_isStop || len <= _m_buffer_productor.WriteableSize();
                });
                if(_m_isStop) return;
            }
            _m_buffer_productor.Push(data, len);
            _m_cond_consumer.notify_one();
        }
        void Stop() {
            _m_isStop = true;
            //调用消费者处理未处理的数据, 消费者还会按需唤醒生产者(safe mode), 生产者写入完成后还会唤醒消费者处理
            _m_cond_consumer.notify_all();
            //_m_cond_productor.notify_all();
            if(_m_thread.joinable()) {
                _m_thread.join();
            }
        }

    private:
        void ConsumerThreadEntry() {
            while(1) {
                {  
                    // 锁用于处理缓冲区swap, 交换后生产者继续写入数据
                    std::unique_lock<std::mutex> lock(_m_mtx);
                    // 有数据时继续, 无数据时阻塞, 等待被生产者唤醒
                    _m_cond_consumer.wait(lock, [&]() {
                        return _m_isStop || !_m_buffer_productor.IsEmpty();
                    });

                    //生产者线程空, 且已经停止, 直接结束
                    if(_m_isStop && _m_buffer_productor.IsEmpty()){
                        return;
                    }

                    _m_buffer_productor.Swap(_m_buffer_consumer);
                    // 固定容量的缓冲区会阻塞生产者, 现在空间足够, 唤醒生产者继续执行
                    if (_m_async_type == AsyncType::ASYNC_SAFE){
                        _m_cond_productor.notify_one();
                    }
                }
                _m_callback_func(_m_buffer_consumer);  // 调用回调函数对消费者缓冲区中数据进行处理
                _m_buffer_consumer.Reset();
            }
        }

    private:
        AsyncType _m_async_type;
        std::atomic<bool> _m_isStop;  // 用于控制异步工作器的启动
        std::mutex _m_mtx;
        //双缓冲区
        Chronicle::Buffer _m_buffer_productor;  //生产者缓冲区, 接收外部写入的数据
        Chronicle::Buffer _m_buffer_consumer;  //消费者缓冲区, 后台线程处理数据
        std::condition_variable _m_cond_productor;
        std::condition_variable _m_cond_consumer;
        std::thread _m_thread;

        CallBackFunc _m_callback_func;  // 回调函数，用来告知工作器如何落地
};
}  // namespace Chronicle