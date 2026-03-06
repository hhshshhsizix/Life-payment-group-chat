# 指定CMake的最低版本要求
cmake_minimum_required(VERSION 3.10)

# 项目名称（可自定义），指定语言为C++
project(Sys_ResidentPayment LANGUAGES CXX)

# 设置C++标准为C++11（网络编程常用，也可根据需求改为C++17等）
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 关闭默认的编译器扩展，保证跨平台兼容性
set(CMAKE_CXX_EXTENSIONS OFF)

# 设置编译选项：开启警告，调试模式下带调试信息
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -Wall -Wextra)
else()
    add_compile_options(-O2 -Wall -Wextra)
endif()

# 设置项目可执行文件输出的路径
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)

# 收集所有需要编译的源文件（.cpp）
set(SOURCE_FILES
src/main.cpp
    src/base/Buffer.cpp
    src/net/Acceptor.cpp
    src/net/Channel.cpp
    src/net/EventLoop.cpp
    src/net/InetAddress.cpp
    src/net/Socket.cpp
    src/net/TcpConnection.cpp
    src/net/TcpServer.cpp
    src/db/ConnectionPool.cpp
    src/db/MysqlConn.cpp
    src/cache/redis.cpp
)

# 收集所有头文件（.h），非必须但便于IDE识别
set(HEADER_FILES
    src/base/Buffer.h
    src/base/Callbacks.h
    src/base/ThreadPool.h
    src/base/tools.h
    src/net/Acceptor.h
    src/net/Channel.h
    src/net/EventLoop.h
    src/net/InetAddress.h
    src/net/Socket.h
    src/net/TcpConnection.h
    src/net/TcpServer.h
    src/db/ConnectionPool.h
    src/db/MysqlConn.h
    src/cache/redis.hpp
    src/http/HttpManager.h
    src/business/UserManager.h
)

# 添加可执行文件目标，指定源文件
add_executable(server ${SOURCE_FILES} ${HEADER_FILES})

# 让编译器能找到头文件
target_include_directories(server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/base
    ${CMAKE_CURRENT_SOURCE_DIR}/src/net
    ${CMAKE_CURRENT_SOURCE_DIR}/src/db
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cache
    ${CMAKE_CURRENT_SOURCE_DIR}/src/http
    ${CMAKE_CURRENT_SOURCE_DIR}/src/business
    ${CMAKE_CURRENT_SOURCE_DIR}/src/3rdparty
)

# 链接依赖库
target_link_libraries(server pthread /usr/lib64/mysql/libmysqlclient.so hiredis ssl crypto)


