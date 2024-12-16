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

# ARMv8-A 架构设置
message(STATUS "Compiling for ARMv8-A architecture.")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DARM -march=armv8-a -mtune=cortex-a72")

# 设置Release和Debug的额外选项
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DDEBUGMODE=0")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUGMODE=1 -ggdb -fsanitize=address -fsanitize=undefined")



set(OPENSSL_ROOT_DIR "/opt/openssl-aarch64")
set(OPENSSL_INCLUDE_DIR "${OPENSSL_ROOT_DIR}/include")
set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_ROOT_DIR}/lib/libcrypto.so")
set(OPENSSL_SSL_LIBRARY "${OPENSSL_ROOT_DIR}/lib/libssl.so")

# 确保 OpenSSL 的头文件路径和库被正确链接
include_directories(${OPENSSL_INCLUDE_DIR})
link_directories(${OPENSSL_ROOT_DIR}/lib)

add_executable(osiris_arm
        src/osiris.cc
        src/executor.cc src/executor.h
        src/code_generator.cc src/code_generator.h
        src/core.cc src/core.h
        src/logger.cc src/logger.h
        src/utils.cc src/utils.h
        src/filter.cc src/filter.h)

target_link_libraries(osiris_arm ${OPENSSL_CRYPTO_LIBRARY} ${OPENSSL_SSL_LIBRARY})
target_link_libraries(osiris_arm capstone)
target_link_libraries(osiris_arm stdc++fs)








set(OPENSSL_ROOT_DIR "/usr")
set(OPENSSL_CRYPTO_LIBRARY "/usr/lib/x86_64-linux-gnu/libcrypto.so")
# 添加可执行文件和源代码
add_executable(osiris
        src/osiris.cc
        src/executor.cc src/executor.h
        src/code_generator.cc src/code_generator.h
        src/core.cc src/core.h
        src/logger.cc src/logger.h
        src/utils.cc src/utils.h
        src/filter.cc src/filter.h)

# 添加依赖库
find_package(OpenSSL REQUIRED)
target_link_libraries(osiris OpenSSL::Crypto)
target_link_libraries(osiris capstone)
target_link_libraries(osiris stdc++fs)
