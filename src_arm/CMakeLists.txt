cmake_minimum_required(VERSION 3.10)
project(osiris)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic")

# 设置默认构建类型
set(default_build_type "Release")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to '${default_build_type}' as none was specified.")
    set(CMAKE_BUILD_TYPE "${default_build_type}")
endif()

# 公共源文件列表
set(SOURCE_FILES
        src/osiris.cc
        src/executor.cc src/executor.h
        src/code_generator.cc src/code_generator.h
        src/core.cc src/core.h
        src/logger.cc src/logger.h
        src/utils.cc src/utils.h
        src/filter.cc src/filter.h
)

# ---- 针对 ARM 平台的配置 ----
if(ARCH STREQUAL "ARM")
    message(STATUS "Configuring for ARMv8-A architecture")

    # 设置编译器标志
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DARM -march=armv8-a -mtune=cortex-a72")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DDEBUGMODE=0")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUGMODE=1 -ggdb -fsanitize=address")

    # 如果是交叉编译的环境，请指定 Capstone 的路径
    set(CAPSTONE_LIB_PATH "/usr/aarch64-linux-gnu/lib")
    find_library(CAPSTONE_LIB NAMES capstone HINTS ${CAPSTONE_LIB_PATH} REQUIRED)

    # 如果 Capstone 安装在其他路径，手动指定
    target_link_libraries(osiris_arm ${CAPSTONE_LIB_PATH}/libcapstone.so)



    # OpenSSL for ARM
    set(OPENSSL_ROOT_DIR "/opt/openssl-aarch64")
    set(OPENSSL_INCLUDE_DIR "${OPENSSL_ROOT_DIR}/include")
    set(OPENSSL_LIB_DIR "${OPENSSL_ROOT_DIR}/lib")

    # 添加可执行文件 osiris_arm
    add_executable(osiris_arm ${SOURCE_FILES})
    include_directories(${OPENSSL_INCLUDE_DIR})
    link_directories(${OPENSSL_LIB_DIR})

    # 链接库
    find_library(CAPSTONE_LIB capstone REQUIRED)
    target_link_libraries(osiris_arm
            ${OPENSSL_LIB_DIR}/libcrypto.so
            ${OPENSSL_LIB_DIR}/libssl.so
            ${CAPSTONE_LIB}
            stdc++fs
    )

    # ---- 针对 x86_64 平台的配置 ----
else()
    message(STATUS "Configuring for x86_64 architecture")

    # 设置编译器标志
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DDEBUGMODE=0")
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUGMODE=1 -ggdb")

    # OpenSSL for x86_64
    set(OPENSSL_ROOT_DIR "/usr")
    set(OPENSSL_CRYPTO_LIBRARY "/usr/lib/x86_64-linux-gnu/libcrypto.so")

    # 添加可执行文件 osiris
    add_executable(osiris ${SOURCE_FILES})
    include_directories("/usr/include")

    # 链接库
    find_package(OpenSSL REQUIRED)
    find_library(CAPSTONE_LIB capstone REQUIRED)
    target_link_libraries(osiris
            OpenSSL::Crypto
            ${CAPSTONE_LIB}
            stdc++fs
    )
endif()
