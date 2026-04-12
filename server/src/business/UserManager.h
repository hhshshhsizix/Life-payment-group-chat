#pragma once
#include <unordered_map>
#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <functional>
#include <any>
#include <iostream>
#include <stdexcept>
#include "TcpConnection.h"
#include "json.hpp"
using json = nlohmann::json;
#include "redis.hpp"
// #include "redispool.h"  // <--- 加入连接池

extern std::string g_server_id;
extern Redis g_redis;
class UserManager {
public:
    static UserManager& getInstance() {
        static UserManager instance;
        return instance;
    }

    void AddConnection(std::string username, TcpConnectionPtr conn) {
        if (conn == nullptr || username.empty()) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_userConnMap[username] = conn;
        conn->setContext(username);
    }

    void RemoveConnection(TcpConnectionPtr conn) {
        if (conn == nullptr) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!conn->hasContext()) return;

        std::string username;
        try {
            username = std::any_cast<std::string>(conn->getContext());
        } catch (...) { return; }
        m_userConnMap.erase(username);
    }

    void SendToUser(const std::string& receiver, const std::string& msg) {
    // 1. 先查本机用户，在线直接发
      std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_userConnMap.find(receiver);
        if (it != m_userConnMap.end()) {
            auto& conn = it->second;
            if (conn && conn->IsConnected()) {
                conn->SendMessage(msg);
            }
            return;
        }
}

    // void SendToremoteUser(std::string receive, std::string from,const std::string& msg)

    // 全服广播
    void Broadcast(const std::string& msg) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& pair : m_userConnMap) {
            auto& conn = pair.second;
            if (conn && conn->IsConnected())
                conn->SendMessage(msg);
        }
    }

void onRedisBroadcastRecv(const std::string& msg)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (msg.empty()) return;

    json j;
    try { j = json::parse(msg); }
    catch (...) {
        std::cerr << "broadcast json parse error\n";
        return;
    }

    if (!j.contains("type")) return;

    if (j.contains("sender_id") && j["sender_id"] == g_server_id) return;

    if (j["type"] == "group_msg")
    {
        // 直接把消息推给所有本地在线用户（和原来 Broadcast 逻辑一致）
        std::string realMsg = j.dump();           // 或者 j["text"]，看你客户端协议
        for (auto& pair : m_userConnMap)
        {
            auto& conn = pair.second;
            if (conn && conn->IsConnected())
                conn->SendMessage(realMsg);
        }
    }
}

void onRedisRecv(const std::string& msg) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (msg.empty()) return;
    json j;
    try {
        j = json::parse(msg);
    } catch (...) {
        std::cerr << "json parse error: " << msg << std::endl;
        return;
    }

    if (!j.contains("type") || !j.contains("receiver") ) {
        std::cerr << "single_chat missing fields: "  << std::endl;
        return;
    }
    std::string type = j["type"];
    if (type != "chat_msg") return;
    std::string receiver = j.value("receiver", "");

    if (receiver.empty()) {
        std::cerr << "single_chat empty field: "  << std::endl;
        return;
    }
    auto it = m_userConnMap.find(receiver);
    if (it != m_userConnMap.end()) {
        auto& conn = it->second;
        if (conn && conn->IsConnected()) {
            conn->SendMessage(j.dump());
        }
    }
}

    std::string FindUserByConn(const TcpConnectionPtr& conn) {
        if (!conn || !conn->hasContext()) return "";
        try { return std::any_cast<std::string>(conn->getContext()); }
        catch (...) { return ""; }
    }

    void RemoveUser(const std::string& username) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_userConnMap.erase(username);
    }

    auto& getuserconnmap()
    {
        return m_userConnMap;
    }

    auto& getmutex()
    {
        return m_mutex;
    }

private:
    UserManager() = default;
    std::unordered_map<std::string, TcpConnectionPtr> m_userConnMap;
    std::mutex m_mutex;
};

