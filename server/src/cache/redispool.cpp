#include "redispool.h"
#include "redis.hpp"
#include <mutex>
#include <condition_variable>
#include <iostream>
using namespace std;

Redis* RedisPool::get() {
    unique_lock<mutex> lock(_mtx);
    _cv.wait(lock, [this] { return !_pool.empty(); });

    Redis* redis = _pool.back();
    _pool.pop_back();
    return redis;
}

void RedisPool::put(Redis* redis) {
    lock_guard<mutex> lock(_mtx);
    _pool.push_back(redis);
    _cv.notify_one();
}

bool RedisPool::init(int pool_size, const string& ip, int port) {
    lock_guard<mutex> lock(_mtx);

    for (int i = 0; i < pool_size; ++i) {
        Redis* r = new Redis();
        if (!r->connect(ip, port)) {
            delete r;
            cerr << "RedisPool connect failed" << endl;
            return false;
        }
        _pool.push_back(r);
    }
    return true;
}

RedisPool::~RedisPool() {
    lock_guard<mutex> lock(_mtx);
    for (auto r : _pool) delete r;
    _pool.clear();
}