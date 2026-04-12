#pragma once

#include <hiredis/hiredis.h>
#include <string>
#include <thread>
#include <functional>
#include <atomic>

//=============================================================================
// 1. 私聊监听器（独立连接、独立线程）
//=============================================================================
class PrivateListener {
public:
    static PrivateListener& instance();
    bool init(const std::string& ip, int port);
    void set_callback(std::function<void(const std::string& msg)> cb);
    ~PrivateListener();

private:
    PrivateListener();
    void run();

    redisContext* _ctx;
    std::thread _th;
    std::function<void(const std::string& msg)> _cb;
    std::atomic<bool> _running;
    std::string _last_id;  // 新增：自己的last_id
};

//=============================================================================
// 2. 广播监听器（独立连接、独立线程）
//=============================================================================
class BroadcastListener {
public:
    static BroadcastListener& instance();
    bool init(const std::string& ip, int port);
    void set_callback(std::function<void(const std::string& msg)> cb);
    ~BroadcastListener();

private:
    BroadcastListener();
    void run();

    redisContext* _ctx;
    std::thread _th;
    std::string _last_id;
    std::function<void(const std::string&)> _cb;
    bool _running;
};

//=============================================================================
// 3. 业务Redis：只发消息，进连接池
//=============================================================================
class Redis {
public:
    Redis();
    ~Redis();

    bool connect(const std::string& ip, int port);

    bool publish(int channel, const std::string& msg);
    bool publish_broadcast(const std::string& msg);

    void set(const std::string& key, const std::string& value);

    bool xadd(const std::string& stream, const std::string& field, const std::string& value);

    void del(const std::string& key);

    std::string get(const std::string& key);
private:
    redisContext* _ctx;
};