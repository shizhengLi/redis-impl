cmake_minimum_required(VERSION 3.10)
project(miniredis VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(include)

add_executable(miniredis src/main.cpp)

# 添加测试
enable_testing()
add_subdirectory(test)

# 编译选项
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0 -Wall -Wextra)
else()
    add_compile_options(-O2)
endif()