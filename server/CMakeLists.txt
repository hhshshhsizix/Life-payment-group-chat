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

# 收集所有需要编译的源文件（.cpp）
set(SOURCE_FILES
    Acceptor.cpp
    Buffer.cpp
    Channel.cpp
    ConnectionPool.cpp
    EventLoop.cpp
    InetAddress.cpp
    main.cpp
    MysqlConn.cpp
    Socket.cpp
    TcpConnection.cpp
    TcpServer.cpp
    redis.cpp
)

# 收集所有头文件（.h），非必须但便于IDE识别
set(HEADER_FILES
    Acceptor.h
    Buffer.h
    Callbacks.h
    Channel.h
    ConnectionPool.h
    EventLoop.h
    HttpManager.h
    InetAddress.h
    MysqlConn.h
    Socket.h
    TcpConnection.h
    TcpServer.h
    ThreadPool.h
    tools.h
    UserManager.h
    redis.hpp
)

# 添加可执行文件目标，指定源文件
add_executable(server ${SOURCE_FILES} ${HEADER_FILES})

# 链接依赖库（根据项目需求补充，比如mysql、pthread等）
# 示例：链接线程库


target_link_libraries(server pthread /usr/lib64/mysql/libmysqlclient.so hiredis ssl crypto)

# 如果需要链接mysql客户端库，取消下面注释并调整路径
# find_library(MYSQL_LIB mysqlclient PATHS /usr/lib64/mysql /usr/lib/mysql)
# if(MYSQL_LIB)
#     target_link_libraries(server ${MYSQL_LIB})
# else()
#     message(FATAL_ERROR "mysqlclient library not found!")
# endif()
