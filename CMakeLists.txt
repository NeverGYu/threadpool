cmake_minimum_required(VERSION 3.22.1)
project(myThreadPool)

# mymuduo最终编译成so动态库，设置动态库路径，放在根目录下的lib文件夹下
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
# 设置调试信息，以及启动C++语言标准
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -std=c++20")
# 定义要参加的源代码文件、
aux_source_directory(. SRC_LIST)
# 编译生成动态库 myThreadPool
add_library(myThreadPool SHARED ${SRC_LIST})