#include <iostream>
#include <sys/file.h>

#include "EventLoop.h"
#include "InetAddress.h"
#include "TcpServer.h"
#include "ThreadPool.h"
#include "MysqlConn.h"
#include "ConnectionPool.h"
#include "HttpManager.h"
#include "tools.h"

#include <openssl/md5.h>
#include <functional>
#include <string_view>
#include <sys/stat.h>
#include <fstream>
#include <ctime>
#include "redis.hpp"
#include "UserManager.h"
#include "redispool.h"
#include <openssl/ssl.h>
#include <openssl/err.h>

SSL_CTX* g_ssl_ctx = nullptr;

void initSSL() {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();

    // 创建TLS服务端上下文
    g_ssl_ctx = SSL_CTX_new(TLS_server_method());
    if (!g_ssl_ctx) {
        ERR_print_errors_fp(stderr);
        exit(1);
    }

    // 加载服务端证书（和Nginx的server.crt/key用同一套即可）
    if (SSL_CTX_use_certificate_chain_file(g_ssl_ctx, "/home/admin/code/chatsys/Life-payment-group-chat/server/server.crt") <= 0) {
        ERR_print_errors_fp(stderr);
        exit(1);
    }
    if (SSL_CTX_use_PrivateKey_file(g_ssl_ctx, "/home/admin/code/chatsys/Life-payment-group-chat/server/server.key", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(1);
    }
    if (!SSL_CTX_check_private_key(g_ssl_ctx)) {
        fprintf(stderr, "Private key does not match the certificate public key\n");
        exit(1);
    }

    // 验证Nginx的客户端证书（双向认证，可选）
    // SSL_CTX_set_verify(g_ssl_ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, NULL);
    // SSL_CTX_load_verify_locations(g_ssl_ctx, "/xxx/ca.crt", NULL);
}

void cleanupSSL() {
    SSL_CTX_free(g_ssl_ctx);
    EVP_cleanup();
    ERR_free_strings();
}

//Redis g_redis;
std::string g_server_id;
const std::string SALT = "WANNABE_YYDS";
const std::string SPECIAL_STRING = "Allinus_yejiliayunachaeryeongryujin20190212";

ThreadPool* g_threadPool = nullptr;

enum class MSG_TYPE : int {
    USER_LOGIN = 0,
    ADMIN_LOGIN
};

void test_Buffer() {
    Buffer buf;
    // 1. 测试初始状态
    std::cout << "Init readable: " << buf.ReadableBytes() << std::endl; // 应为 0
    std::cout << "Init writable: " << buf.WriteableBytes() << std::endl; // 应为 1024

    // 2. 测试写入
    std::string msg = "Hello World";
    buf.Append(msg);
    std::cout << "After write readable: " << buf.ReadableBytes() << std::endl; // 应为 11

    // 3. 测试读取 (Peek)
    std::string peekStr(buf.peek(), buf.ReadableBytes());
    std::cout << "Peek content: " << peekStr << std::endl; // Hello World

    // 4. 测试 Retrieve (消费)
    buf.rm_ReadIndex(6); // 消费 "Hello "
    std::cout << "After retrieve 6, readable: " << buf.ReadableBytes() << std::endl; // 应为 5
    std::cout << "Left content: " << buf.ReadAllAsString() << std::endl; // World

    // 5. 测试 readFd (需要一个真实的文件或 socket，这里简单模拟一下)
    // 假设你随便创建一个 test.txt 文件，内容是 abcdefg...
    int fd = open("test.txt", O_RDONLY);
    if (fd < 0) {
        std::cout << "open failed!!!!" << errno << std::endl;
    }
    ssize_t len = buf.ReadFd(fd);
    std::cout << "file len " << len << std::endl;
    std::cout << "file data: " << buf.ReadAllAsString() << std::endl;
    std::cout << "Buffer test pass!" << std::endl;
}

void onConnection(const TcpConnectionPtr& conn) {
    if (conn->IsConnected()) {
        
        std::cout << "New Connection: " << conn->GetClientAddress().ToIpPort() << std::endl;
    }
    else {
        std::string user = UserManager::getInstance().FindUserByConn(conn);
        if (!user.empty()) {
            UserManager::getInstance().RemoveConnection(conn);

        Redis* redis = RedisPool::instance().get();
            if (redis) {
                redis->del("user_router:" + user);
                RedisPool::instance().put(redis);
        
        std::cout << "[Logout] 用户[" << user << "]路由从Redis删除" << std::endl;
    }
        }
       
    }
}

bool verifyLoginSign(const std::string& username, const std::string& password, int64_t timestamp, const std::string sign) {
    
    std::string raw = username + password + std::to_string(timestamp) + SALT  + SPECIAL_STRING;
    unsigned char md5_digest[MD5_DIGEST_LENGTH];
    MD5(reinterpret_cast<const unsigned char*>(raw.data()), raw.size(), md5_digest);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (int i = 0; i < 16; ++i) {
        oss << std::setw(2) << static_cast<unsigned int>(md5_digest[i]);
    }

    std::string s_sign = oss.str();
    return s_sign == sign;
}

void onMessage(const TcpConnectionPtr& conn, Buffer* buf) {
    
    //看一眼数据的开头前4个字符
    std::string firstBytes = buf->ReadAsString(4, true); 
    if (firstBytes == "POST" || firstBytes == "GET ") {
        HttpRequest request;
        while (request.Parse(buf))
        {
            std::string url_action = request.m_action;
            json json_request = request.m_jsonData;

            g_threadPool->enqueue([conn, json_request, url_action]() { // 传递纯净的json数据
                std::cout << "[Thread " << std::this_thread::get_id() << "]processing JSON..." << std::endl;

                json response_json;

                std::shared_ptr<MysqlConn> mysqlConn = ConnectionPool::getConnectionPool()->getConnection();
                if (!mysqlConn) {
                    // 如果池子满了或连不上数据库
                    json err = HttpResponse::SetError("服务器忙，无法获取数据库连接!!!");
                    HttpResponse::SendHttpResponse(conn, err, "500 Internal Server Error");
                    return;
                }

                try {

                    if (url_action == "/login") {

                        std::string username = json_request["username"];
                        MSG_TYPE type = json_request["type"];
                        int64_t timestamp = json_request["timestamp"].get<int64_t>();
                        std::string sign = json_request["sign"]; //签名
                        char sql[512] = { 0 };

                        if (type == MSG_TYPE::USER_LOGIN) { // 居民登录
                            // 查找居民表
                            sprintf(sql, "SELECT password FROM user_info WHERE username='%s'", username.c_str());
                            MYSQL_RES* res = mysqlConn->query(sql);
                            if (!res) return;
                            if (mysql_num_rows(res) == 0) { // 没找到用户
                                response_json["role"] = "user";
                                response_json["status"] = "fail";
                                response_json["message"] = "User not found";
                                HttpResponse::SendHttpResponse(conn, response_json, "200 OK");
                                std::cout << "用户[" << username << "]尝试登录，---未找到该用户" << std::endl;
                                mysql_free_result(res);
                                return;
                            }
                            //mysqlConn->DebugPrintResult(res); // 测试查询结果

                            MYSQL_ROW row = mysql_fetch_row(res); // 拿到行
                            if (!row) return;
                            std::string db_pwd = row[0]; // 拿到密码
                            // 进行签名比对，验证密码是否正确
                            bool cmp = verifyLoginSign(username, db_pwd, timestamp, sign);
                            if (cmp) {
                                sprintf(sql, "SELECT realname, phone, sex, building, floor, housenum, balance FROM user_info WHERE username='%s'", username.c_str());
                                res = mysqlConn->query(sql);
                                row = mysql_fetch_row(res);
                                if (row != nullptr) {
                                    response_json["status"] = "success";
                                    response_json["message"] = "登录成功!";
                                
                                    json user_data;
                                    user_data["username"] = username; // 用户名直接用请求里的
                                    user_data["realname"] = row[0] ? row[0] : "";
                                    user_data["phone"] = row[1] ? row[1] : "";
                                    user_data["sex"] = row[2] ? atoi(row[2]) : 0; // 数字类型
                                    user_data["building"] = row[3] ? row[3] : ""; 
                                    user_data["floor"] = row[4] ? atoi(row[4]) : 0;
                                    user_data["housenum"] = row[5] ? atoi(row[5]) : 0;
                                    user_data["balance"] = row[6] ? std::stod(row[6]) : 0.00;
                                    std::cout << row[6] << std::endl;
                                    response_json["data"] = user_data;
                                }
                                
                                std::cout << "用户[" << username << "]已登录" << std::endl;
                                
                            }
                            else {
                                response_json["role"] = "user";
                                response_json["status"] = "fail";
                                response_json["message"] = "Wrong password";
                                std::cout << "用户[" << username << "]尝试登录，---密码错误" << std::endl;
                            }
                            mysql_free_result(res);
                        }
                        else if (type == MSG_TYPE::ADMIN_LOGIN) { // 管理员登录
                            sprintf(sql, "SELECT password FROM root_info WHERE username='%s'", username.c_str());
                            MYSQL_RES* res = mysqlConn->query(sql);
                            if (!res) return;
                            if (mysql_num_rows(res) == 0) {
                                response_json["role"] = "admin";
                                response_json["status"] = "fail";
                                response_json["message"] = "Admin not found";
                                std::cout << "管理员[" << username << "]尝试登录，---无权限" << std::endl;
                                HttpResponse::SendHttpResponse(conn, response_json, "200 OK");
                                mysql_free_result(res);
                                return;
                            }
                            MYSQL_ROW row = mysql_fetch_row(res);
                            if (!row) return;
                            std::string db_pwd = row[0];
                            bool cmp = verifyLoginSign(username, db_pwd, timestamp, sign);
                            if (cmp) {
                                response_json["role"] = "admin";
                                response_json["status"] = "success";
                                response_json["message"] = "登录成功!";
                                std::cout << "管理员[" << username << "]已登录" << std::endl;
                            }
                            else {
                                response_json["role"] = "admin";
                                response_json["status"] = "fail";
                                response_json["message"] = "Wrong password";
                                std::cout << "管理员[" << username << "]尝试登录，---密钥错误" << std::endl;
                            }
                            mysql_free_result(res);
                        }
                    }
                    else if (url_action == "/register") {
                        std::string username = json_request["username"];
                        std::string password = json_request["password"];
                        std::string realname = json_request["realname"];
                        std::string phone = json_request["phone"];
                        
                        char sql[1024] = { 0 };
                        sprintf(sql, "INSERT INTO user_info(username, password, realname, phone) VALUES('%s', '%s', '%s', '%s')",
                            username.c_str(), password.c_str(), realname.c_str(), phone.c_str());
                        bool res = mysqlConn->update(sql);
                        if (!res) {
                            response_json["role"] = "user";
                            response_json["status"] = "fail";
                            response_json["message"] = "注册失败!";
                            HttpResponse::SendHttpResponse(conn, response_json, "200 OK");
                            std::cout << "注册用户[" << username << "]失败!!!" << std::endl;
                            return;
                        }
                        else {
                            response_json["status"] = "success";
                            response_json["message"] = "注册成功!";
                            response_json["username"] = username;
                            std::cout << "用户[" << username << "]已注册" << std::endl;
                        }
                    }
                    else if (url_action == "/retrieve") {
                        std::string username = json_request["username"];
                        std::string realname = json_request["realname"];
                        std::string phone = json_request["phone"];
                        char sql[512] = { 0 };
                        sprintf(sql, "SELECT realname, phone FROM user_info WHERE username='%s'", username.c_str());
                        MYSQL_RES* res = mysqlConn->query(sql);
                        if (!res) return;
                        if (mysql_num_rows(res) == 0) { // 没找到用户
                            response_json["status"] = "fail";
                            response_json["message"] = "未找到该用户，请重新输入!!!";
                            HttpResponse::SendHttpResponse(conn, response_json, "200 OK");
                            mysql_free_result(res);
                            return;
                        }
                        MYSQL_ROW row = mysql_fetch_row(res); // 拿到行
                        if (!row) return;
                        std::string db_realname = row[0];
                        std::string db_phone = row[1];
                        if ((db_realname == realname) && (db_phone == phone)) {
                            response_json["status"] = "success";
                            response_json["message"] = "匹配成功";
                            std::cout << "用户[" << username << "]已可以修改密码了" << std::endl;
                        }
                        else {
                            response_json["status"] = "fail";
                            response_json["message"] = "个人信息输入错误，无权更改密码!!!";
                            HttpResponse::SendHttpResponse(conn, response_json, "200 OK");
                            std::cout << "申请修改用户[" << username << "]密码失败!!!" << std::endl;
                            mysql_free_result(res);
                            return;
                        }
                        mysql_free_result(res);
                    }
                    else if (url_action == "/changePwd") {
                        std::string username = json_request["username"];
                        std::string password = json_request["password"];
                        char sql[512] = { 0 };
                        sprintf(sql, "UPDATE user_info SET password = '%s' WHERE username = '%s'", password.c_str(), username.c_str());
                        bool res = mysqlConn->update(sql);
                        if (!res) {
                            response_json["status"] = "fail";
                            response_json["message"] = "数据库执行错误，更改密码失败!!!";
                            HttpResponse::SendHttpResponse(conn, response_json, "200 OK");
                            std::cout << "修改用户[" << username << "]密码失败!!!" << std::endl;
                            return;
                        }
                        else {
                            response_json["status"] = "success";
                            response_json["message"] = "密码修改成功!";
                            std::cout << "用户[" << username << "]密码已修改" << std::endl;
                        }
                    }
                    else if (url_action == "/saveAvatar") {
                        std::string username = json_request["username"];
                        std::string avatar = json_request["avatar"];

                        std::stringstream sqlStream;
                        sqlStream << "UPDATE user_info SET avatar = '" << avatar << "' WHERE username = '" << username << "'";
                        std::string sqlStr = sqlStream.str();
                        bool res = mysqlConn->update(sqlStr);
                        if (!res) {
                            response_json["status"] = "fail";
                            response_json["message"] = "上传头像失败!";
                            HttpResponse::SendHttpResponse(conn, response_json, "200 OK");
                            std::cout << "保存用户[" << username << "]头像失败!!!" << std::endl;
                            return;
                        }
                        response_json["status"] = "success";
                        response_json["message"] = "头像上传成功!";
                        std::cout << "保存用户[" << username << "]头像成功!" << std::endl;
                    }
                    else if (url_action == "/getAvatar") {
                        std::string username = json_request["username"];
                        bool isSelf = json_request["isSelf"];
                        std::stringstream ss;
                        ss << "SELECT avatar FROM user_info WHERE username = '" << username << "'";

                        MYSQL_RES* res = mysqlConn->query(ss.str());
                        if (!res) return;
                        MYSQL_ROW row = mysql_fetch_row(res); // 拿到行
                        if (!row) { // 没找到用户
                            response_json["status"] = "fail";
                            response_json["message"] = "未找到该用户!!!";
                            HttpResponse::SendHttpResponse(conn, response_json, "200 OK");
                            mysql_free_result(res);
                            return;
                        }
                        std::string avatarData = "";
                        if (row[0] != NULL) {
                            avatarData = row[0]; // 将 char* 转为 std::string
                        }
                        response_json["status"] = "success";
                        response_json["message"] = "头像获取成功!";
                        response_json["avatar"] = avatarData;
                        if (!isSelf) response_json["sender"] = username;
                        else response_json["sender"] = "";
                        std::cout << "发送用户[" << username << "]头像成功!" << std::endl;
                        mysql_free_result(res);

                    }
                    else if (url_action == "/getNotices") {

                        std::string sql = "SELECT title, content, sender, level, "
                            "DATE_FORMAT(publish_time, '%Y-%m-%d %H:%i:%s') as p_time "
                            "FROM notices " 
                            "ORDER BY level DESC, publish_time DESC";

                        MYSQL_RES* res = mysqlConn->query(sql);
                        if (!res) {
                            response_json["status"] = "error";
                            response_json["message"] = "Database query failed";
                            return;
                        }

                        // 构造JSON数组
                        json noticeArray = json::array();
                        MYSQL_ROW row;

                        while ((row = mysql_fetch_row(res))) {
                            json item;
                            item["title"] = (row[0] ? row[0] : "");
                            item["content"] = (row[1] ? row[1] : "");
                            // 发送人为空则显示 Admin
                            item["sender"] = (row[2] ? row[2] : "Admin");
                            // 紧急程度
                            item["level"] = (row[3] ? std::stoi(row[3]) : 1);
                            // 时间
                            item["time"] = (row[4] ? row[4] : "");

                            noticeArray.push_back(item);
                        }
                        mysql_free_result(res);

                        // 发送成功响应
                        response_json["status"] = "success";
                        response_json["message"] = "发送公告成功";
                        response_json["data"] = noticeArray;
                        std::cout << "发送公告成功!" << std::endl;
                    }
                    else if (url_action == "/pushNotice") {

                        std::string title = json_request["title"];
                        std::string content = json_request["content"];
                        std::string sender = json_request["sender"];
                        std::string publish_time = json_request["time"];
                        int level = json_request["level"];

                        char sql[1024] = { 0 };
                        sprintf(sql, "INSERT INTO notices (title, content, sender, level, publish_time) VALUES ('%s', '%s', '%s', %d, '%s')",
                                title.c_str(), content.c_str(), sender.c_str(), level, publish_time.c_str());
                            
                        bool res = mysqlConn->update(sql);
                        if (!res) {
                            response_json["status"] = "fail";
                            response_json["message"] = "发布公告失败!";
                            HttpResponse::SendHttpResponse(conn, response_json, "200 OK");
                            return;
                        }
                        response_json["status"] = "success";
                        response_json["message"] = "发布公告成功";

                        std::string groupMsg = "{\"type\":\"pushNotice\"}";
                        UserManager::getInstance().Broadcast(groupMsg + "\n");
                    }
                    else if (url_action == "/logout") {
                        std::string username = json_request["username"];
                        UserManager::getInstance().RemoveUser(username);
                        return;
                    }
                    else if (url_action == "/changeInfo") {
                        std::string username = json_request["username"];
                        auto data = json_request["data"];
                        std::string realname = data["realname"];
                        std::string phone = data["phone"];
                        int sex = data["sex"];
                        std::string building = data["building"];
                        int floor = data["floor"];
                        int housenum = data["housenum"];

                        char sql[512] = { 0 };
                        sprintf(sql, "UPDATE user_info SET realname='%s', phone='%s', sex=%d, building='%s', floor=%d, housenum='%d' WHERE username='%s'",
                            realname.c_str(), phone.c_str(), sex, building.c_str(), floor, housenum, username.c_str());
                        bool res = mysqlConn->update(sql);
                        if (!res) {
                            response_json["status"] = "fail";
                            response_json["message"] = "数据库执行错误，修改个人信息失败!!!";
                            HttpResponse::SendHttpResponse(conn, response_json, "200 OK");
                            std::cout << "修改用户[" << username << "]信息失败!!!" << std::endl;
                            return;
                        }
                        else {
                            response_json["status"] = "success";
                            response_json["message"] = "个人信息修改成功!";
                            std::cout << "用户[" << username << "]信息已修改" << std::endl;
                        }
                    }
                    else if (url_action == "/delivery") {
                        std::string username = json_request["username"];
                        std::string content = json_request["content"];
                        int level = json_request["level"];
                        int status = json_request["status"];
                        std::string time = json_request["time"];
                        int type = json_request["type"];
                        auto imageArray = json_request["images"];
                        std::string images_path = "";
                        std::string upload_dir = "/home/yejisu/projects/Sys_ResidentPayment/images/"; // 图片存储区域
                        struct stat st = { 0 };
                        if (stat(upload_dir.c_str(), &st) == -1) {
                            mkdir(upload_dir.c_str(), 0777);
                        }
                        
                        for (auto& item : imageArray) {
                            std::string base64_str = item;

                            // 去掉可能存在的前缀 "data:image/jpeg;base64,"
                            size_t comma = base64_str.find(",");
                            if (comma != std::string::npos) {
                                base64_str = base64_str.substr(comma + 1);
                            }

                            // 解码
                            std::vector<unsigned char> data = base64_decode(base64_str);

                            // 生成唯一文件名 (这里简单用时间戳+随机数，防止重名)
                            char filename[64];
                            sprintf(filename, "%ld_%d.jpg", std::time(nullptr), rand() % 10000);
                            std::string full_path = upload_dir + filename;

                            // 写入文件 (二进制模式)
                            std::ofstream outfile(full_path, std::ios::out | std::ios::binary);
                            if (outfile.is_open()) {
                                outfile.write(reinterpret_cast<const char*>(data.data()), data.size());
                                outfile.close();

                                // 拼接到路径字符串
                                if (!images_path.empty()) images_path += ";";
                                images_path += (std::string(filename)); // 存相对路径
                            }
                        }
                        
                        char sql[2048] = { 0 };
                        sprintf(sql, "INSERT INTO troubles (username, content, level, status, type, time, images) VALUES ('%s', '%s', %d, %d, %d, '%s', '%s'); ",
                            username.c_str(), content.c_str(), level, status, type, time.c_str(), images_path.c_str() // 这里存的是拼接好的路径字符串 "path1;path2"
                        );
                        bool res = mysqlConn->update(sql);

                        if (!res) {
                            response_json["status"] = "fail";
                            response_json["message"] = "数据库执行错误，报修提交失败!!!";
                            HttpResponse::SendHttpResponse(conn, response_json, "200 OK");
                            std::cout << "用户[" << username << "]提交报修失败!!!" << std::endl;
                            return;
                        }
                        else {
                            response_json["status"] = "success";
                            response_json["message"] = "报修提交成功!";
                            std::cout << "用户[" << username << "]报修已提交, 图片数:" << imageArray.size() << std::endl;

                            std::string groupMsg = "{\"type\":\"pushDelivery\"}";
                            UserManager::getInstance().SendToUser("root", groupMsg + "\n");
                        }
                        
                    } 
                    else if (url_action == "/getprogress") {
                        std::string username = json_request.value("username", "");

                        char sql[1024] = { 0 };
                        if(username == "") sprintf(sql, "SELECT status, type, content, username, level, images FROM troubles ORDER BY status, level DESC, time DESC");
                        else sprintf(sql, "SELECT status, type, content FROM troubles WHERE username = '%s' ORDER BY level, time DESC", username.c_str());
                        MYSQL_RES* res = mysqlConn->query(sql);
                        if (!res) return;
                        if (mysql_num_rows(res) == 0) { // 没找到用户
                            response_json["status"] = "fail";
                            response_json["message"] = "无报修或投诉历史!!!";
                            HttpResponse::SendHttpResponse(conn, response_json, "200 OK");
                            mysql_free_result(res);
                            return;
                        }
                        json dataArray = json::array(); // json数组，用来存每个投诉信息
                        MYSQL_ROW row;
                        if (username == "") {
                            while (row = mysql_fetch_row(res)) {
                                json item;
                                item["status"] = (row[0] ? std::stoi(row[0]) : 0);
                                item["type"] = (row[1] ? std::stoi(row[1]) : 0);
                                item["content"] = (row[2] ? row[2] : "");
                                std::string username = (row[3] ? row[3] : "");
                                item["username"] = username;
                                item["level"] = (row[4] ? std::stoi(row[4]) : 0);
                                // 获取图片路径
                                item["iamgesPath"] = (row[5] ? row[5] : "");
                                dataArray.push_back(item);

                                response_json["role"] = "root";
                            }
                        }
                        else {
                            while (row = mysql_fetch_row(res)) {
                                json item;
                                item["status"] = (row[0] ? std::stoi(row[0]) : 0);
                                item["type"] = (row[1] ? std::stoi(row[1]) : 0);
                                item["content"] = (row[2] ? row[2] : "");
                                dataArray.push_back(item);

                                response_json["role"] = "user";

                            }
                        }
                        response_json["status"] = "success";
                        response_json["message"] = "匹配成功";
                        response_json["data"] = dataArray;
                        std::cout << "用户[" << username << "]查询投诉历史成功!" << std::endl;
                        
                        mysql_free_result(res);
                    }
                    else if (url_action == "/getBills") {
                        std::string username = json_request["username"];

                        char sql[1024] = { 0 };
                        json dataArray = json::array();
                        std::string database[6] = {"water_info", "elect_info", "gas_info" , "heating_info" , "parking_info" , "property_info" };
                        for (int i = 0; i < 6; i++) {
                            std::string dbName = database[i];
                            memset(sql, 0, sizeof sql);
                            sprintf(sql, "SELECT spend, subInfo, dataList FROM %s WHERE username = '%s'", dbName.c_str(), username.c_str());
                            MYSQL_RES* res = mysqlConn->query(sql);
                            if (!res) return;
                            if (mysql_num_rows(res) == 0) { // 没找到用户
                                response_json["status"] = "fail";
                                response_json["message"] = "未找到用户信息!!!";
                                HttpResponse::SendHttpResponse(conn, response_json, "200 OK");
                                mysql_free_result(res);
                                return;
                            }
                            MYSQL_ROW row = mysql_fetch_row(res);
                            json item;
                            item["title"] = dbName;
                            item["spend"] = (row[0] ? std::stod(row[0]) : 0.00);
                            item["subInfo"] = (row[1] ? row[1] : "");
                            if (row[2] && strlen(row[2]) > 0) {
                                try {
                                    item["dataList"] = json::parse(row[2]);  // 推荐：存JSON字符串
                                }
                                catch (...) {
                                    item["dataList"] = json::array();
                                }
                            }
                            else {
                                item["dataList"] = json::array();
                            }

                            dataArray.push_back(item);

                            mysql_free_result(res);
                        }
                        response_json["status"] = "success";
                        response_json["message"] = "匹配成功";
                        response_json["data"] = dataArray;
                        std::cout << "用户[" << username << "]查询账单成功!" << std::endl;
                    }
                    else if (url_action == "/setBills") {
                        std::string username = json_request["username"];
                        double balance = json_request["balance"];
                        double waterspend = json_request["waterSpend"];
                        double electspend = json_request["electSpend"];
                        double gasspend = json_request["gasSpend"];
                        std::string parkDate = json_request["parkSpend"];
                        std::string propertyDate = json_request["propertySpend"];

                        double arrD[3] = { waterspend, electspend, gasspend };
                        std::string arrS[2] = { parkDate, propertyDate };

                        char sql[512] = { 0 };
                        sprintf(sql, "UPDATE user_info SET balance=%.2f WHERE username='%s'", balance, username.c_str());
                        bool res = mysqlConn->update(sql);
                        if (!res) return;
                        std::string database[5] = { "water_info", "elect_info", "gas_info" , "parking_info" , "property_info" };
                        for (int i = 0; i < 3; i++) {
                            std::string dbName = database[i];
                            double spend = arrD[i];
                            memset(sql, 0, sizeof sql);
                            sprintf(sql, "UPDATE %s SET spend=%.2f WHERE username='%s'", dbName.c_str(), spend, username.c_str());
                            res = mysqlConn->update(sql);
                            if (!res) return;
                        }
                        for (int i = 3; i < 5; i++) {
                            std::string dbName = database[i];
                            std::string dbSubInfo = arrS[i - 3];
                            memset(sql, 0, sizeof sql);
                            sprintf(sql, "UPDATE %s SET subInfo='%s' WHERE username='%s'", dbName.c_str(), dbSubInfo.c_str() , username.c_str());
                            res = mysqlConn->update(sql);
                            if (!res) return;
                        }
                        
                        response_json["status"] = "success";
                        response_json["message"] = "缴费成功";
                        std::cout << "用户[" << username << "]账单缴费成功!" << std::endl;
                    }
                    else if (url_action == "/setPaymentList") {
                        std::string username = json_request["username"];
                        auto items = json_request["items"];
                        char sql[512] = { 0 };
                        for (auto item : items) {
                            std::string title = item["title"];
                            std::string time = item["time"];
                            double amount = item["amount"];
                            sprintf(sql, "INSERT INTO payBills (username, title, time, amount) VALUES ('%s', '%s', '%s', %.2f); ",
                                username.c_str(), title.c_str(), time.c_str(), amount);
                            bool res = mysqlConn->update(sql);
                            if (!res) return;
                        }
                        response_json["status"] = "success";
                        response_json["message"] = "缴费记录写入成功";
                        std::cout << "用户[" << username << "]发送缴费记录成功!" << std::endl;
                    }
                    else if (url_action == "/getPaymentList") {
                        std::string username = json_request["username"];
                        char sql[1024] = { 0 };
                        sprintf(sql, "SELECT title, time, amount FROM payBills WHERE username = '%s' ORDER BY time DESC", username.c_str());
                        MYSQL_RES* res = mysqlConn->query(sql);
                        if (!res) return;
                        if (mysql_num_rows(res) == 0) { // 没找到用户
                            response_json["status"] = "fail";
                            response_json["message"] = "无缴费历史!!!";
                            HttpResponse::SendHttpResponse(conn, response_json, "200 OK");
                            mysql_free_result(res);
                            return;
                        }
                        json dataArray = json::array(); // json数组，用来存每个投诉信息
                        MYSQL_ROW row;
                        while (row = mysql_fetch_row(res)) {
                            json item;
                            item["title"] = (row[0] ? row[0] : "");
                            item["time"] = (row[1] ? row[1] : "");
                            item["amount"] = (row[2] ? std::stod(row[2]) : 0.00);
                            dataArray.push_back(item);
                        }
                        response_json["data"] = dataArray;
                        response_json["status"] = "success";
                        response_json["message"] = "缴费记录写入成功";
                        std::cout << "用户[" << username << "]获取缴费记录成功!" << std::endl;
                        mysql_free_result(res);
                    }
                    else if (url_action == "/getAllInfo") {
                        // 获取用户信息
                        char sql[1024] = { 0 };
                        sprintf(sql, "SELECT username, realname, phone, building, floor, housenum, balance FROM user_info");
                        MYSQL_RES* res = mysqlConn->query(sql);
                        if (!res) return;
                        if (mysql_num_rows(res) == 0) { // 没找到用户
                            response_json["status"] = "fail";
                            response_json["message"] = "无用户注册!!!";
                            HttpResponse::SendHttpResponse(conn, response_json, "200 OK");
                            mysql_free_result(res);
                            return;
                        }
                        json dataArray = json::array();
                        MYSQL_ROW row;
                        std::string database[5] = { "water_info", "elect_info", "gas_info" , "parking_info" , "property_info" };
                        std::string datatype[5][2] = { {"water_spend","water_info" }, {"elect_spend","elect_info" }, {"gas_spend","gas_info" }, {"parking_date"}, {"property_date"} };
                        while (row = mysql_fetch_row(res)) {
                            json item;
                            std::string username = (row[0] ? row[0] : "");
                            item["username"] = username;
                            item["realname"] = (row[1] ? row[1] : "");
                            item["phone"] = (row[2] ? row[2] : "");
                            item["building"] = (row[3] ? row[3] : "");
                            item["floor"] = (row[4] ? std::stod(row[4]) : 0);
                            item["housenum"] = (row[5] ? std::stod(row[5]) : 0);
                            item["balance"] = (row[6] ? std::stod(row[6]) : 0.00);
                            
                            for (int i = 0; i < 3; i++) {
                                std::string dbName = database[i];
                                std::string dbType0 = datatype[i][0];
                                std::string dbType1 = datatype[i][1];
                                char sql2[512] = { 0 };
                                sprintf(sql2, "SELECT spend, subInfo FROM %s WHERE username='%s'", dbName.c_str(), username.c_str());
                                MYSQL_RES* res2 = mysqlConn->query(sql2);
                                if (!res2) return;
                                MYSQL_ROW row2 = mysql_fetch_row(res2);
                                if (row2 == nullptr) {
                                    item[dbType0] = 0.00;
                                    item[dbType1] = "";
                                }
                                else {
                                    item[dbType0] = (row2[0] ? std::stod(row2[0]) : 0.00);
                                    item[dbType1] = (row2[1] ? row2[1] : "");
                                }
                                mysql_free_result(res2);
                            }
                            for (int i = 3; i < 5; i++) {
                                std::string dbName = database[i];
                                std::string dbType0 = datatype[i][0];
                                char sql2[512] = { 0 };
                                sprintf(sql2, "SELECT subInfo FROM %s WHERE username='%s'", dbName.c_str(), username.c_str());
                                MYSQL_RES* res2 = mysqlConn->query(sql2);
                                if (!res2) return;
                                MYSQL_ROW row2 = mysql_fetch_row(res2);
                                if (row2 == nullptr) item[dbType0] = "";
                                else item[dbType0] = (row2[0] ? row2[0] : "");
                                mysql_free_result(res2);
                            }
                            
                            // 查室温
                            char sql3[512] = { 0 };
                            sprintf(sql3, "SELECT spend, subInfo FROM heating_info WHERE username='%s'", username.c_str());
                            MYSQL_RES* res3 = mysqlConn->query(sql3);
                            if (!res3) return;
                            MYSQL_ROW row3 = mysql_fetch_row(res3);
                            if (row3 == nullptr) {
                                item["temperature"] = 0.00;
                                item["area"] = "";
                            }
                            else {
                                item["temperature"] = (row3[0] ? std::stod(row3[0]) : 0.00);
                                item["area"] = (row3[1] ? row3[1] : "");
                            }
                            dataArray.push_back(item);
                            mysql_free_result(res3);
                        }
                        response_json["data"] = dataArray;
                        response_json["status"] = "success";
                        response_json["message"] = "获取用户信息成功";
                        std::cout << "管理员获取所有用户信息成功!" << std::endl;
                        mysql_free_result(res);
                    }
                    else if (url_action == "/getDeliveryInfo") {
                        std::string username = json_request["username"];
                        std::string imgPath = json_request["iamgesPath"];
                        char sql[512] = { 0 };
                        sprintf(sql, "SELECT phone, building, floor, housenum FROM user_info WHERE username = '%s'", username.c_str());
                        MYSQL_RES* res = mysqlConn->query(sql);
                        if (!res) return;
                        MYSQL_ROW row = mysql_fetch_row(res);
                        response_json["phone"] = (row[0] ? row[0] : "");
                        std::string address = (row[1] ? row[1] : "");
                        address += std::to_string(row[2] ? std::stoi(row[2]) : 0) + "层";
                        address += std::to_string(row[3] ? std::stoi(row[3]) : 0) + "室";
                        response_json["address"] = address;
                        json dataArray = json::array();
                        // 获取每一张图片的路径
                        if (imgPath != "") {
                            std::vector<std::string> paths;
                            std::stringstream ss(imgPath);
                            std::string token;
                            while (std::getline(ss, token, ';')) {
                                if (!token.empty()) {
                                    paths.push_back(token);
                                }
                            }
                            std::string upload_dir = "/home/yejisu/projects/Sys_ResidentPayment/images/"; // 图片存储区域
                            // 获取图片
                            for (auto i : paths) {
                                json item;
                                std::string full_path = upload_dir + i; // 绝对路径
                                std::ifstream file(full_path, std::ios::binary | std::ios::ate);
                                if (file.is_open()) {
                                    // 获取文件大小
                                    std::streamsize size = file.tellg();
                                    file.seekg(0, std::ios::beg);

                                    std::vector<char> buffer(size);
                                    if (file.read(buffer.data(), size)) {
                                        std::string base64 = base64_encode(reinterpret_cast<const unsigned char*>(buffer.data()), buffer.size());
                                        item["image"] = base64;
                                    }
                                }
                                dataArray.push_back(item);
                            }
                        }
                        response_json["images"] = dataArray;
                        response_json["status"] = "success";
                        response_json["message"] = "获取信息成功";
                        std::cout << "管理员获取报修/投诉信息成功!" << std::endl;
                        mysql_free_result(res);
                    }
                    else if (url_action == "/updateDeliveryStatus") {
                        std::string username = json_request["username"];
                        std::string content = json_request["content"];
                        int status = json_request["status"];
                        char sql[512] = { 0 };
                        sprintf(sql, "UPDATE troubles SET status = %d WHERE username = '%s' AND content = '%s'", status, username.c_str(), content.c_str());
                        bool res = mysqlConn->update(sql);
                        if (!res) return;
                        response_json["status"] = "success";
                        response_json["message"] = "修改进度成功";
                        std::cout << "管理员修改进度成功!" << std::endl;

                        std::string groupMsg = "{\"type\":\"updateDelivery\"}";
                        UserManager::getInstance().SendToUser(username, groupMsg + "\n");
                    }

                    //-----------------------------------------------------------------------------------------

                    HttpResponse::SendHttpResponse(conn, response_json, "200 OK");
                    std::cout << "服务器回复完毕！！！" << std::endl;
                }
                catch (json::parse_error& e) {
                    // JSON解析失败
                    response_json["message"] = "无效的JSON格式,未解析到action";
                    // 发送 400 Bad Request响应
                    HttpResponse::SendHttpResponse(conn, response_json, "400 Bad Request");
                }
            });
            
        }
    }
    else {
        std::string msgStr = buf->ReadAllAsString();
        json j = json::parse(msgStr);
        if (j["type"] == "chat_login") {
            std::string username = j["username"];
            UserManager::getInstance().AddConnection(username, conn);
            Redis* redis = RedisPool::instance().get();
            if (redis) {
                std::string server_id = g_server_id;
                redis->set("user_router:" + username, server_id);
                RedisPool::instance().put(redis);
                std::cout << "[Login] 用户[" << username << "]路由写入Redis: " << server_id << std::endl;
            }
        }
        else if (j["type"] == "group_msg") {
            json boardcastJson = j;
            boardcastJson["sender_id"] = g_server_id;
            std::string broadcastMsg = boardcastJson.dump();
            UserManager::getInstance().Broadcast(broadcastMsg);
            Redis* redis = RedisPool::instance().get();
            redis->publish_broadcast(broadcastMsg);      // <--- 改这里！！！
            RedisPool::instance().put(redis);

            std::cout << "group message: " << j["text"];
        }
        else if (j["type"] == "chat_msg") {
            std::string from = j["from"];
            std::string receiver = j["receiver"];
            std::string content = j["text"];
            std::cout << "to " << receiver << ":" << content << std::endl;
            
            bool isLocal = false;
    {
        std::lock_guard<std::mutex> lock(UserManager::getInstance().getmutex());
        auto it = UserManager::getInstance().getuserconnmap().find(receiver);
        if (it != UserManager::getInstance().getuserconnmap().end()) {
            auto& conn = it->second;
            if (conn && conn->IsConnected()) {
                // 给接收方本地发送，不走Redis
                conn->SendMessage(msgStr);
                isLocal = true;
            }
        }

        // 给自己发一份回显（无论本地/跨服都能看到）
        auto self_it = UserManager::getInstance().getuserconnmap().find(from);
        if (self_it != UserManager::getInstance().getuserconnmap().end()) {
            auto& self_conn = self_it->second;
            if (self_conn && self_conn->IsConnected()) {
                self_conn->SendMessage(msgStr);
            }
        }
    }
    // 同服务器用户，直接返回，不进Redis
    if (isLocal) return;
            Redis* redis = RedisPool::instance().get();
            std::string target_server = redis->get("user_router:" + receiver);
            std::string target_stream = "stream_server_" + target_server;
            bool ok = redis->xadd(target_stream, "msg", msgStr);
            if (ok) {
                std::cout << "[SendToUser] 消息成功写入Stream: " << target_stream << ", 接收方: " << receiver << std::endl;
            } else {
                std::cerr << "[SendToUser] 写入Stream失败: " << target_stream << std::endl;
            }
            RedisPool::instance().put(redis);
        }
        else if (j["type"] == "ping") {
            // TODO:心跳包处理 
                json pong;
                pong["type"] = "pong";
                pong["username"] = j["username"];  // 回显用户名，方便客户端识别
                conn->SendMessage(pong.dump());
        }
    }
}

int Test(uint16_t mainPort, uint16_t chatPort) {
    std::cout << "Main thread starting..." << std::endl;
    g_server_id = std::to_string(chatPort);

    PrivateListener::instance().init("127.0.0.1", 6379);
    PrivateListener::instance().set_callback([](const std::string& msg) {   // 新增这个函数
    UserManager::getInstance().onRedisRecv(msg);
    });

    BroadcastListener::instance().init("127.0.0.1", 6379);
    BroadcastListener::instance().set_callback([](const std::string& msg) {   // 新增这个函数
    UserManager::getInstance().onRedisBroadcastRecv(msg);
    });
    
    //g_redis.subscribe(100); // 全服公用频道
    static ThreadPool* pool = new ThreadPool(4); // 增加线程数到8，应对高并发
    g_threadPool = pool;

    // 核心：单个EventLoop + 多线程TcpServer（分散IO压力）
    EventLoop mainLoop1;
    // 主Reactor 2：专门给 聊天服务
    EventLoop mainLoop2;

    // HTTP服务（2551）：设置4个工作线程处理连接
    InetAddress listenAddr(mainPort); 
    TcpServer server(&mainLoop1, listenAddr);
    server.SetConnectionCallback(onConnection);
    server.SetRecvMessageCallback(onMessage);
    server.SetThreadsNum(2); // 关键：给HTTP服务分配4个线程
    server.Start();

    // 聊天服务（4390）：设置2个工作线程处理连接
    InetAddress chatAddr(chatPort);
    TcpServer chatserver(&mainLoop2, chatAddr);
    chatserver.enableTls();
    chatserver.SetConnectionCallback(onConnection);
    chatserver.SetRecvMessageCallback(onMessage);
    chatserver.SetThreadsNum(2); // 关键：给聊天服务分配2个线程
    chatserver.Start();

    std::thread t1([&]() {
        std::cout << "HTTP MainLoop thread started..." << std::endl;
        mainLoop1.Loop();
    });

    std::thread t2([&]() {
        std::cout << "Chat MainLoop thread started..." << std::endl;
        mainLoop2.Loop();
    });

    // 等待两个主循环
    t1.join();
    t2.join();
    return 0;
}

int main(int argc, char* argv[]) {
    initSSL();
    //test_Buffer();
    // 默认端口
    uint16_t mainPort = 2552;
    uint16_t chatPort = 4389;

    RedisPool::instance().init(8, "127.0.0.1", 6379);

    // 解析命令行参数
    if (argc >= 2) {
        mainPort = static_cast<uint16_t>(atoi(argv[1]));
    }
    if (argc >= 3) {
        chatPort = static_cast<uint16_t>(atoi(argv[2]));
    }

    // 简单校验端口合法性A
    if (mainPort < 1024 || mainPort > 65535 || chatPort < 1024 || chatPort > 65535) {
        std::cerr << "端口非法！请输入 1024-65535 之间的端口" << std::endl;
        return -1;
    }
    Test(mainPort, chatPort);
	return 0;
}
