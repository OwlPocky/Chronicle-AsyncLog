/*日志缓冲区类，生产者消费者模型统一使用的缓冲区*/
#pragma once
#include <cassert>
#include <string>
#include <vector>
#include "Util.hpp"

//单例模式确保了全局唯一实例, extern声明实现跨文件共享
extern Chronicle::Util::JsonData *g_conf_data;  //声明JsonData地址
//redefine错误, cpp有重复定义, hpp只能声明
//Chronicle::Util::JsonData *g_conf_data = Chronicle::Util::JsonData::GetJsonData();

namespace Chronicle{
    //日志缓冲区类
    class Buffer{
    public:
        Buffer() : _m_write_pos(0), _m_read_pos(0) {
            _m_buffer.resize(g_conf_data->buffer_size);
        }

        //向缓冲区写入数据, 并自动扩容
        void Push(const char *data, size_t len){
            CheckAndReserve(len);   //保证容量充足
            //写入[data, data+len)到&_m_buffer[_m_write_pos]
            std::copy(data, data + len, &_m_buffer[_m_write_pos]);
            _m_write_pos += len;
        }

        //获取可读数据的起始地址, 需要指定读取的长度
        char* ReadBegin(size_t len){
            assert(len <= ReadableSize());
            return &_m_buffer[_m_read_pos];
        }

        //获取可读数据的起始地址
        const char *Begin() { 
            return &_m_buffer[_m_read_pos]; 
        }

        // 剩余可写入大小(字节)
        size_t WriteableSize(){ 
            return _m_buffer.size() - _m_write_pos;
        }
        // 当前可读取大小(字节)
        size_t ReadableSize(){
            return _m_write_pos - _m_read_pos;
        }

        //向后移动写指针, 移动长度len字节
        void MoveWritePos(size_t len){
            assert(len <= WriteableSize());
            _m_write_pos += len;
        }
        
        //向后移动读指针, 移动长度len字节
        void MoveReadPos(size_t len){
            assert(len <= ReadableSize());
            _m_read_pos += len;
        }

        //交换两个缓冲区的底层数据和读写指针状态, 无锁切换
        void Swap(Buffer &buf){
            _m_buffer.swap(buf._m_buffer);
            std::swap(_m_read_pos, buf._m_read_pos);
            std::swap(_m_write_pos, buf._m_write_pos);
        }

        //判断缓冲区是否为空
        bool IsEmpty() { 
            return _m_write_pos == _m_read_pos; 
        }

        //重置缓冲区
        void Reset(){
            _m_write_pos = 0;
            _m_read_pos = 0;
        }

    protected:
        // 缓冲区扩容:
        // - 容量小于阈值时, 按倍数扩容(指数增长)
        // - 容量超过阈值时, 按固定值扩容(线性增长)
        void CheckAndReserve(size_t len){
            size_t buffersize = _m_buffer.size();
            //cout << "buffersize = " << buffersize << endl;
            if (len > WriteableSize()){
                /*需要扩容*/
                if (buffersize < g_conf_data->threshold){
                    _m_buffer.resize(2 * buffersize);
                }
                else{
                    _m_buffer.resize(g_conf_data->linear_growth + buffersize);
                }
                //cout << "CheckAndReserve: len from " << buffersize << " to " << _m_buffer.size() << endl;
            }
        }

    protected:
        std::vector<char> _m_buffer; // 缓冲区, 初始大小g_conf_data->buffer_size
        size_t _m_write_pos;         // 生产者写指针的偏移量
        size_t _m_read_pos;          // 消费者消费者的偏移量
    };
} // namespace Chronicle