cmake_minimum_required(VERSION 3.20)
project(HighPerformanceLoadBalancer
    VERSION 1.0.0
    DESCRIPTION "High-Performance C++20 HTTP Reverse Proxy and Load Balancer"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler Warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wformat=2
    )
endif()

# Sanitizers
option(ENABLE_ASAN "Enable Address & Undefined Behavior Sanitizers" OFF)
option(ENABLE_TSAN "Enable Thread Sanitizer" OFF)
option(ENABLE_FUZZING "Enable Fuzz Testing target" OFF)

if(ENABLE_ASAN AND ENABLE_TSAN)
    message(FATAL_ERROR "ASan and TSan cannot be enabled simultaneously.")
endif()

if(ENABLE_ASAN)
    message(STATUS "Enabling AddressSanitizer (ASan) & UndefinedBehaviorSanitizer (UBSan)")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

if(ENABLE_TSAN)
    message(STATUS "Enabling ThreadSanitizer (TSan)")
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=thread)
endif()

# Core Include Directory
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Core Library Sources
set(CORE_SOURCES
    src/network/socket.cpp
    src/network/connection.cpp
    src/http/http_types.cpp
    src/http/http_parser.cpp
    src/http/request.cpp
    src/http/response.cpp
    src/threading/thread_pool.cpp
    src/server/tcp_server.cpp
    src/server/server.cpp
    src/balancer/load_balancer.cpp
    src/balancer/round_robin.cpp
    src/balancer/least_connections.cpp
    src/health/health_checker.cpp
    src/monitoring/metrics.cpp
    src/logging/logger.cpp
    src/config/config.cpp
    src/proxy/reverse_proxy.cpp
)

add_library(load_balancer_core STATIC ${CORE_SOURCES})
target_include_directories(load_balancer_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(load_balancer_core PUBLIC pthread)

# Main Executable
add_executable(load_balancer_server src/main.cpp)
target_link_libraries(load_balancer_server PRIVATE load_balancer_core)

# Testing
include(CTest)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
