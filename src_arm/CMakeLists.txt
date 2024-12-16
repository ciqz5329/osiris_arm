cmake_minimum_required(VERSION 3.10)
project(osiris)

# 默认的构建类型为 Release
set(default_build_type "Release")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to '${default_build_type}' as none was specified.")
    set(CMAKE_BUILD_TYPE "${default_build_type}")
endif()

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")

# 针对 ARM 设置编译选项
if (ARCH STREQUAL ARM)
    message(STATUS "Compiling for ARM processor.")
    # 设置 ARM 架构的编译标志
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DARM -march=armv8-a -mtune=cortex-a72 -mfpu=neon")
else()
    message(WARNING "Not compiling for ARM. Default settings will be applied.")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DINTEL")
endif()

# 设置构建类型的编译选项
set(CMAKE_CXX_FLAGS_RELEASE  "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DDEBUGMODE=0")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUGMODE=1 -ggdb -fsanitize=address -fsanitize=undefined")

# 添加可执行文件
add_executable(osiris
        src/osiris.cc
        src/executor.cc src/executor.h
        src/code_generator.cc src/code_generator.h
        src/core.cc src/core.h
        src/logger.cc src/logger.h
        src/utils.cc src/utils.h
        src/filter.cc src/filter.h)

# 添加依赖
find_package(OpenSSL REQUIRED)
target_link_libraries(osiris OpenSSL::Crypto)
target_link_libraries(osiris capstone)
target_link_libraries(osiris stdc++fs)
