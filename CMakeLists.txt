cmake_minimum_required(VERSION 3.20)
project(DistributedInfrastructureControlPlane CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Prefix paths for custom-installed packages if present on local host
if(DEFINED ENV{HOME} AND EXISTS "$ENV{HOME}/.local/usr")
    list(APPEND CMAKE_PREFIX_PATH "$ENV{HOME}/.local/usr" "$ENV{HOME}/.local")
    include_directories("$ENV{HOME}/.local/usr/include")
    link_directories("$ENV{HOME}/.local/usr/lib/x86_64-linux-gnu" "$ENV{HOME}/.local/usr/lib")
endif()

# Compiler warnings and optimizations
add_compile_options(-Wall -Wextra -Wpedantic -O2 -g)

# Find Required Packages
find_package(Threads REQUIRED)

if(DEFINED ENV{HOME} AND EXISTS "$ENV{HOME}/.local/bin/protoc")
    set(Protobuf_PROTOC_EXECUTABLE "$ENV{HOME}/.local/bin/protoc")
endif()
find_package(Protobuf REQUIRED)
if(NOT Protobuf_PROTOC_EXECUTABLE)
    find_program(Protobuf_PROTOC_EXECUTABLE protoc REQUIRED)
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Generate Protocol Buffers into ${CMAKE_CURRENT_BINARY_DIR}/proto
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/proto)

set(PROTO_NAMES common membership registry configuration gossip)
set(PROTO_SRCS)
set(PROTO_HDRS)

foreach(proto_name ${PROTO_NAMES})
    set(src "${CMAKE_CURRENT_BINARY_DIR}/proto/${proto_name}.pb.cc")
    set(hdr "${CMAKE_CURRENT_BINARY_DIR}/proto/${proto_name}.pb.h")
    list(APPEND PROTO_SRCS ${src})
    list(APPEND PROTO_HDRS ${hdr})

    add_custom_command(
        OUTPUT ${src} ${hdr}
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        ARGS -I=${CMAKE_CURRENT_SOURCE_DIR}/proto
             --cpp_out=${CMAKE_CURRENT_BINARY_DIR}/proto
             ${CMAKE_CURRENT_SOURCE_DIR}/proto/${proto_name}.proto
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/proto/${proto_name}.proto
        COMMENT "Running protoc on proto/${proto_name}.proto"
    )
endforeach()

add_library(dicp_proto STATIC ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(dicp_proto PUBLIC ${Protobuf_LIBRARIES})
target_include_directories(dicp_proto PUBLIC 
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${Protobuf_INCLUDE_DIRS}
)
if(DEFINED ENV{HOME} AND EXISTS "$ENV{HOME}/.local/usr/include")
    target_include_directories(dicp_proto PUBLIC "$ENV{HOME}/.local/usr/include")
endif()

# Core Library
set(CORE_SOURCES
    core/common/config.cpp
    core/networking/message_frame.cpp
    core/networking/udp_transport.cpp
    core/networking/tcp_transport.cpp
    core/agent/node_identity.cpp
    core/agent/node_agent.cpp
    core/agent/agent_http_server.cpp
    core/membership/membership_manager.cpp
    core/membership/failure_detector.cpp
    core/gossip/dissemination_queue.cpp
    core/gossip/gossip_engine.cpp
    core/registry/service_registry.cpp
    core/health/health_monitor.cpp
    core/load_balancer/http_proxy.cpp
    core/config_manager/config_manager.cpp
    core/storage/sqlite_storage.cpp
)

add_library(dicp_core STATIC ${CORE_SOURCES})
add_dependencies(dicp_core dicp_proto)
target_link_libraries(dicp_core PUBLIC 
    dicp_proto 
    Threads::Threads 
    sqlite3
)

# Agent Executable
add_executable(dicp_agent core/agent/main.cpp)
add_dependencies(dicp_agent dicp_proto)
target_link_libraries(dicp_agent PRIVATE dicp_core)

# Load Balancer Executable
add_executable(dicp_lb core/load_balancer/main.cpp)
add_dependencies(dicp_lb dicp_proto)
target_link_libraries(dicp_lb PRIVATE dicp_core)

# Tests
enable_testing()

find_library(GTEST_LIB gtest PATHS "$ENV{HOME}/.local/usr/lib/x86_64-linux-gnu" "$ENV{HOME}/.local/usr/lib" /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu)
find_library(GTEST_MAIN_LIB gtest_main PATHS "$ENV{HOME}/.local/usr/lib/x86_64-linux-gnu" "$ENV{HOME}/.local/usr/lib" /usr/lib /usr/local/lib /usr/lib/x86_64-linux-gnu /usr/lib/aarch64-linux-gnu)

macro(add_dicp_test test_name test_source)
    add_executable(${test_name} ${test_source})
    add_dependencies(${test_name} dicp_proto)
    target_link_libraries(${test_name} PRIVATE 
        dicp_core 
        ${GTEST_LIB} 
        ${GTEST_MAIN_LIB} 
        Threads::Threads
    )
    add_test(NAME ${test_name} COMMAND ${test_name})
endmacro()

add_dicp_test(test_config tests/unit/test_config.cpp)
add_dicp_test(test_message_frame tests/unit/test_message_frame.cpp)
add_dicp_test(test_node_identity tests/unit/test_node_identity.cpp)
add_dicp_test(test_udp_transport tests/unit/test_udp_transport.cpp)
add_dicp_test(test_membership tests/unit/test_membership.cpp)
add_dicp_test(test_failure_detector tests/unit/test_failure_detector.cpp)
add_dicp_test(test_cluster_convergence tests/unit/test_cluster_convergence.cpp)
add_dicp_test(test_service_registry tests/unit/test_service_registry.cpp)
add_dicp_test(test_load_balancer tests/unit/test_load_balancer.cpp)
add_dicp_test(test_config_manager tests/unit/test_config_manager.cpp)
add_dicp_test(test_sqlite_storage tests/unit/test_sqlite_storage.cpp)
