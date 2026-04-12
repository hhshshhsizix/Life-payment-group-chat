#pragma once
#include <vector>
#include <mutex>
#include <condition_variable>
#include "redis.hpp"  // ✅ 在这里包含，获得Redis的完整类定义

class RedisPool {
public:
    static RedisPool& instance() {
        static RedisPool pool;
        return pool;
    }

    ~RedisPool();

    // 仅声明方法，不实现
    Redis* get();
    void put(Redis* redis);
    bool init(int pool_size, const std::string& ip, int port); // 仅声明

private:
    std::vector<Redis*> _pool;
    std::mutex _mtx;
    std::condition_variable _cv;
};