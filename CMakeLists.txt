# 最低 CMake 版本
cmake_minimum_required(VERSION 3.0)

# 项目名
project(tftp)

# 使用 C++11
set(CMAKE_CXX_STANDARD 11)

# 头文件搜索路径
include_directories(${CMAKE_SOURCE_DIR}/include)

# 客户端可执行文件
add_executable(tftp_client
    src/client/client_main.cpp
    src/client/client.cpp
    src/client/upload.cpp
    src/client/download.cpp
    src/common.cpp
)

# 服务器可执行文件
add_executable(tftp_server
    src/server/server_main.cpp
    src/server/server.cpp
    src/server/do_upload.cpp
    src/server/do_download.cpp
    src/common.cpp
)

# 两个可执行文件生成到项目根目录
set_target_properties(tftp_client tftp_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}
)