#pragma once
#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

class ThreadPool {
public:
    ThreadPool(size_t threads)
        : _m_isStop(false) {
        for (size_t i = 0; i < threads; ++i){
            //创建新线程
            //该线程从任务队列_m_tasks中取一个任务, 并执行
            _m_workers.emplace_back([this](){
                //std::function<void(void)> task;
                std::function<void()> task;
                //每个线程都是无限循环, 从任务队列中获取任务
                for (;;){
                    {
                        std::unique_lock<std::mutex> lock(this->_m_mtx);
                        // 等待任务队列不为空或线程池停止
                        this->_m_condition.wait(lock, [this](){ return this->_m_isStop || !this->_m_tasks.empty(); });
                        //如果退出且当前线程任务全部执行完毕, 退出
                        if (this->_m_isStop && this->_m_tasks.empty()){
                            return;
                        }
                        //取出任务
                        task = std::move(this->_m_tasks.front());
                        this->_m_tasks.pop();
                    }
                    // 执行任务
                    task();
                }
            });
        }
    }

    //将新任务添加到任务队列中, 返回这个异步任务的std::future对象, 用于获取任务的执行结果
    //args: func, ...args
    template <class F, class... Args>
    auto enqueue(F &&f, Args &&...args)
        -> std::future<typename std::result_of<F(Args...)>::type>{
        //result_of可推导出可调用对象的返回类型
        using return_type = typename std::result_of<F(Args...)>::type;

        // 根据future对象绑定func和args..., 生成可调用对象
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<return_type> task_future = task->get_future();
        {
            std::unique_lock<std::mutex> lock(_m_mtx);

            if (_m_isStop){
                throw std::runtime_error("ThreadPool::enqueue");
            }
            // 将任务添加到任务队列
            _m_tasks.emplace([task](){ 
                //(*task)()()才是真正运行task函数
                //std::packaged_task函数对象包装器重载了operator()来执行内部封装的任务
                //(*task)();  // 解引用指针，然后调用 operator()
                // 等价于
                //task->operator()();  // 使用箭头操作符直接调用成员函数
                (*task)(); 
            });
        }
        _m_condition.notify_one();
        return task_future;
    }

    ~ThreadPool(){
        {
            std::unique_lock<std::mutex> lock(_m_mtx);
            _m_isStop = true;
        }
        _m_condition.notify_all();
        for (std::thread &worker : _m_workers)
        {
            worker.join();
        }
    }

private:
    std::vector<std::thread> _m_workers;        // 线程
    std::queue<std::function<void()>> _m_tasks; // 任务队列(所有线程共享)
    std::mutex _m_mtx;                          // 任务队列的互斥量
    std::condition_variable _m_condition;       // 用于任务队列的同步
    bool _m_isStop;
};