#pragma once
#include <unordered_map>
#include <string>
#include <vector>
#include <mutex>
#include <memory>
#include <functional>
#include "TcpConnection.h"
#include "json.hpp"
using json = nlohmann::json;
#include "redis.hpp"
extern Redis g_redis;
extern std::string g_server_id;
class UserManager {
public:
    static UserManager& getInstance() {
        static UserManager instance;
        return instance;
    }

    // 登录绑定连接（本地）
    void AddConnection(std::string username, TcpConnectionPtr conn) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_userConnMap[username] = conn;
        conn->setContext(username);
    }

    // 断开连接
    void RemoveConnection(TcpConnectionPtr conn) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!conn->hasContext()) return;

        std::string username;
        try {
            username = std::any_cast<std::string>(conn->getContext());
        } catch (...) { return; }

        m_userConnMap.erase(username);
    }

    // 单聊（自动跨服）
    void SendToUser(std::string receiver, const std::string& msg) {
        std::lock_guard<std::mutex> lock(m_mutex);

        auto it = m_userConnMap.find(receiver);
        if (it != m_userConnMap.end()) {
            auto& conn = it->second;
            if (conn && conn->IsConnected()) {
                conn->SendMessage(msg + "\n");
            }
            return;
        }

        // 本地没有，发 Redis 跨服
        g_redis.publish(100, msg);
    }

    // 全服广播（跨服）
    void Broadcast(const std::string& msg) {
        std::lock_guard<std::mutex> lock(m_mutex);

        // 本机发
        for (auto& pair : m_userConnMap) {
            auto& conn = pair.second;
            if (conn->IsConnected()) conn->SendMessage(msg + "\n");
        }

        // 发 Redis，让其他服务器也广播
        json j;
        j["type"] = "cross_broadcast";
        j["msg"] = msg;
        j["sender_id"] = g_server_id; // 标记是谁发的
        g_redis.publish(100, j.dump());
    }

    // Redis 消息入口
    void onRedisRecv(const std::string& msg) {
        std::lock_guard<std::mutex> lock(m_mutex);
        json j = json::parse(msg);

        

        if (j["type"] == "cross_broadcast") {
            if (j["sender_id"] == g_server_id) {
                return;
        }
            std::string realMsg = j["msg"];
            for (auto& pair : m_userConnMap) {
                auto& conn = pair.second;
                if (conn->IsConnected()) conn->SendMessage(realMsg + "\n");
            }
            return;
        }

        // 普通单聊
        std::string receiver = j["receiver"];
        auto it = m_userConnMap.find(receiver);
        if (it != m_userConnMap.end()) {
            auto& conn = it->second;
            if (conn && conn->IsConnected()) {
                conn->SendMessage(msg);
            }
        }
    }

    // 下面这些你原来的逻辑不动
    std::string FindUserByConn(const TcpConnectionPtr& conn) {
        if (conn->hasContext()) return std::any_cast<std::string>(conn->getContext());
        return "";
    }

    void RemoveUser(const std::string& username) {
        m_userConnMap.erase(username);
    }

private:
    UserManager() = default;
    std::unordered_map<std::string, TcpConnectionPtr> m_userConnMap;
    std::mutex m_mutex;
};