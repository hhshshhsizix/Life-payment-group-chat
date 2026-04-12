#include "redis.hpp"
#include <iostream>
#include <string>
using namespace std;


extern std::string g_server_id;

PrivateListener::PrivateListener()
    : _ctx(nullptr), _running(true), _last_id("0") {}  // 初始从0开始读

PrivateListener::~PrivateListener() {
    _running = false;
    if (_th.joinable()) _th.join();
    if (_ctx) redisFree(_ctx);
}

PrivateListener& PrivateListener::instance() {
    static PrivateListener inst;
    return inst;
}

bool PrivateListener::init(const string& ip, int port) {
    _ctx = redisConnect(ip.c_str(), port);
    if (!_ctx || _ctx->err) return false;

    // 启动时从Redis加载上次读到的last_id，避免重启重复读
    redisReply* r = (redisReply*)redisCommand(_ctx,
        "GET lastid:private_%s", g_server_id.c_str());
    if (r && r->type == REDIS_REPLY_STRING) {
        _last_id = r->str;
    }
    if (r) freeReplyObject(r);

    _th = thread(&PrivateListener::run, this);
    cout << "[PrivateListener] 启动成功，监听Stream: stream_server_" << g_server_id << endl;
    return true;
}

void PrivateListener::set_callback(function<void(const string& msg)> cb) {
    _cb = move(cb);
}

void PrivateListener::run() {
    // 自己的专属Stream：stream_server_端口
    string self_stream = "stream_server_" + g_server_id;
    cout << "[PrivateListener] Thread started, stream: " << self_stream << ", last_id: " << _last_id << endl;

    while (_running) {
        // XREAD 读自己的Stream，block 5秒等新消息
        redisReply* r = (redisReply*)redisCommand(_ctx,
            "XREAD COUNT 1 BLOCK 5000 STREAMS %s %s",
            self_stream.c_str(), _last_id.c_str());

        if (!r || r->type != REDIS_REPLY_ARRAY || r->elements == 0) {
            if (r) freeReplyObject(r);
            continue;
        }

        auto stream = r->element[0];
        if (!stream || stream->type != REDIS_REPLY_ARRAY || stream->elements < 2) {
            freeReplyObject(r);
            continue;
        }

        auto msgs = stream->element[1];
        if (!msgs || msgs->type != REDIS_REPLY_ARRAY) {
            freeReplyObject(r);
            continue;
        }

        // 遍历本次读到的所有消息
        string new_last_id = _last_id;
        for (size_t i = 0; i < msgs->elements; ++i) {
            auto msg = msgs->element[i];
            if (!msg || msg->type != REDIS_REPLY_ARRAY || msg->elements < 2) {
                continue;
            }

            // 更新last_id为最新的消息ID
            if (msg->element[0] && msg->element[0]->str) {
                new_last_id = msg->element[0]->str;
            }

            auto fields = msg->element[1];
            if (!fields || fields->type != REDIS_REPLY_ARRAY) {
                continue;
            }

            string content;
            for (size_t j = 0; j+1 < fields->elements; j += 2) {
                const char* key_str = fields->element[j]->str;
                const char* val_str = fields->element[j+1]->str;
                if (!key_str || !val_str) continue;

                string k = key_str;
                string v = val_str;
                if (k == "msg") {  // 对应SendToUser写入的field
                    content = v;
                    break;
                }
            }

            // 回调给业务层处理私聊消息
            if (_cb && !content.empty()) {
                _cb(content);
            }
        }

        // 更新本地last_id
        _last_id = new_last_id;
        // 同步到Redis，重启后从这里继续读
        redisReply* ack_r = (redisReply*)redisCommand(_ctx,
            "SET lastid:private_%s %s", g_server_id.c_str(), _last_id.c_str());
        if (ack_r) freeReplyObject(ack_r);

        freeReplyObject(r);
    }
}


// BroadcastListener 广播监听

BroadcastListener::BroadcastListener()
    : _ctx(nullptr), _last_id("$"), _running(true) {}

BroadcastListener::~BroadcastListener() {
    _running = false;
    if (_th.joinable()) _th.join();
    if (_ctx) redisFree(_ctx);
}

BroadcastListener& BroadcastListener::instance() {
    static BroadcastListener inst;
    return inst;
}

bool BroadcastListener::init(const string& ip, int port) {
    _ctx = redisConnect(ip.c_str(), port);
    if (!_ctx || _ctx->err) return false;

    _th = thread(&BroadcastListener::run, this);
    cout << "[BroadcastListener] 启动成功" << endl;
    return true;
}

void BroadcastListener::set_callback(function<void(const string& msg)> cb) {
    _cb = move(cb);
}

void BroadcastListener::run() {
    while (_running) {
        redisReply* r = (redisReply*)redisCommand(_ctx,
            "XREAD COUNT 10 BLOCK 5000 STREAMS broadcast_stream %s",
            _last_id.c_str());

        if (!r || r->type != REDIS_REPLY_ARRAY || r->elements == 0) {
            if (r) freeReplyObject(r);
            continue;
        }

        auto stream = r->element[0];
        if (!stream || stream->type != REDIS_REPLY_ARRAY || stream->elements < 2) {
            freeReplyObject(r);
            continue;
        }

        auto msgs = stream->element[1];
        if (!msgs || msgs->type != REDIS_REPLY_ARRAY) {
            freeReplyObject(r);
            continue;
        }

        for (size_t i = 0; i < msgs->elements; ++i) {
            auto msg = msgs->element[i];
            if (!msg || msg->type != REDIS_REPLY_ARRAY || msg->elements < 2) {
                continue;
            }

            if (msg->element[0] && msg->element[0]->str) {
                _last_id = msg->element[0]->str;
            }

            auto fields = msg->element[1];
            if (!fields || fields->type != REDIS_REPLY_ARRAY) {
                continue;
            }

            string content;
            for (size_t j = 0; j+1 < fields->elements; j += 2) {
                const char* key_str = fields->element[j]->str;
                const char* val_str = fields->element[j+1]->str;

                if (!key_str || !val_str) continue;

                string k = key_str;
                string v = val_str;

                if (k == "message") content = v;
            }

            if (_cb && !content.empty()) {
                _cb(content);
            }
        }
        freeReplyObject(r);
    }
}


// Redis 业务发送（进连接池）

Redis::Redis() : _ctx(nullptr) {}
Redis::~Redis() { if (_ctx) redisFree(_ctx); }

bool Redis::connect(const string& ip, int port) {
    _ctx = redisConnect(ip.c_str(), port);
    return _ctx && !_ctx->err;
}

bool Redis::xadd(const string& stream, const string& field, const string& value) {
    if (!_ctx) return false;
    
    redisReply* r = (redisReply*)redisCommand(_ctx,
        "XADD %s * %s %s",
        stream.c_str(), field.c_str(), value.c_str()
    );
    
    // 成功返回消息ID（REDIS_REPLY_STRING）
    bool ok = r && (r->type == REDIS_REPLY_STRING) && !_ctx->err;
    
    if (r) freeReplyObject(r);
    return ok;
}

bool Redis::publish_broadcast(const string& msg) {
    if (!_ctx) return false;
    const char* argv[] = {"XADD", "broadcast_stream", "*", "message", msg.c_str()};
    size_t lens[] = {4,16,1,7, msg.size()};
    redisReply* r = (redisReply*)redisCommandArgv(_ctx,5,argv,lens);
    bool ok = r && r->type == REDIS_REPLY_STRING;
    if (r) freeReplyObject(r);
    return ok;
}

void Redis::set(const string& key, const string& value) {
    if (!_ctx) return;
    redisReply* r = (redisReply*)redisCommand(_ctx, "SET %s %s", key.c_str(), value.c_str());
    if (r) freeReplyObject(r);
}

void Redis::del(const string& key) {
    if (!_ctx) return;
    redisReply* r = (redisReply*)redisCommand(_ctx, "DEL %s", key.c_str());
    if (r) freeReplyObject(r);
}

string Redis::get(const string& key) {
    if (!_ctx) return "";
    redisReply* r = (redisReply*)redisCommand(_ctx, "GET %s", key.c_str());
    string res = "";
    if (r && r->type == REDIS_REPLY_STRING) {
        res = r->str;
    }
    if (r) freeReplyObject(r);
    return res;
}