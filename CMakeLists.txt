cmake_minimum_required(VERSION 3.20)

option(ANOLIS_PROVIDER_EZO_WARNINGS_AS_ERRORS "Treat first-party compiler warnings as errors" OFF)

project(anolis_provider_ezo VERSION 0.3.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(CTest)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Warnings.cmake)

# ThreadSanitizer (TSAN) - data-race detection for the BusExecutor worker thread.
# Applied to all targets below; deps are rebuilt with TSAN via the x64-linux-tsan triplet.
option(ENABLE_TSAN "Enable ThreadSanitizer for data race detection" OFF)

if(ENABLE_TSAN)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        if(NOT WIN32)
            add_compile_options(-fsanitize=thread -g -fno-omit-frame-pointer)
            add_link_options(-fsanitize=thread)
            message(STATUS "ThreadSanitizer (TSAN) enabled for data race detection")
        else()
            message(WARNING "ThreadSanitizer not supported on Windows (Linux/macOS only)")
        endif()
    else()
        message(WARNING "ThreadSanitizer requested but compiler not supported: ${CMAKE_CXX_COMPILER_ID}")
    endif()
# Fallback: detect TSAN triplet automatically.
elseif(DEFINED VCPKG_TARGET_TRIPLET AND VCPKG_TARGET_TRIPLET MATCHES "tsan")
    if(NOT WIN32)
        message(STATUS "ThreadSanitizer auto-detected via triplet: ${VCPKG_TARGET_TRIPLET}")
        add_compile_options(-fsanitize=thread -g -fno-omit-frame-pointer)
        add_link_options(-fsanitize=thread)
    endif()
endif()

find_package(Protobuf REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(Threads REQUIRED)

include(FetchContent)

# ezo-driver: set EZO_DRIVER_DIR to use a local source-tree override; otherwise fetched from release.
if(DEFINED EZO_DRIVER_DIR AND NOT EZO_DRIVER_DIR STREQUAL "")
    if(NOT EXISTS "${EZO_DRIVER_DIR}/CMakeLists.txt")
        message(FATAL_ERROR
            "ezo-driver not found at: ${EZO_DRIVER_DIR}\n"
            "Set EZO_DRIVER_DIR to the root of the ezo-driver repository.")
    endif()
    set(EZO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(EZO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(EZO_BUILD_ARDUINO_ADAPTER OFF CACHE BOOL "" FORCE)
    set(EZO_BUILD_LINUX_ADAPTER OFF CACHE BOOL "" FORCE)
    set(EZO_BUILD_POSIX_UART_ADAPTER OFF CACHE BOOL "" FORCE)
    add_subdirectory("${EZO_DRIVER_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/ezo-driver" EXCLUDE_FROM_ALL)
    if(NOT TARGET ezo::ezo_core)
        add_library(ezo::ezo_core ALIAS ezo_core)
    endif()
else()
    FetchContent_Declare(
        ezo_driver
        URL https://github.com/feastorg/ezo-driver/archive/refs/tags/v0.5.1.tar.gz
        URL_HASH SHA256=dd751af78652b5cce5667887ba1e48bf3ade9a4653273f849f3f545cf2aa5def
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(EZO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(EZO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(EZO_BUILD_ARDUINO_ADAPTER OFF CACHE BOOL "" FORCE)
    set(EZO_BUILD_LINUX_ADAPTER OFF CACHE BOOL "" FORCE)
    set(EZO_BUILD_POSIX_UART_ADAPTER OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(ezo_driver)
    if(NOT TARGET ezo::ezo_core)
        add_library(ezo::ezo_core ALIAS ezo_core)
    endif()
endif()

# The shared SDK owns the anolis-protocol pin + the `adpp_proto` re-export (and the
# `protocol.pb.h` shim), so ezo no longer declares them — a second `adpp_proto`
# target / divergent pin would be a hard error. Consumed as source (pinned release
# tarball); local dev overrides with -DFETCHCONTENT_SOURCE_DIR_ANOLIS_PROVIDER_SDK=<path>.
FetchContent_Declare(
    anolis_provider_sdk
    URL https://github.com/anolishq/anolis-provider-sdk/releases/download/v0.1.2/anolis-provider-sdk-0.1.2-source.tar.gz
    URL_HASH SHA256=06c2620e3d62a250a72dcf44955cd65e6f13b8ce13b752bbd5bf4cda07ed2d85
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(anolis_provider_sdk)

add_library(anolis_provider_ezo_support STATIC
    src/config/provider_config.cpp
    src/logging/logger.cpp)
target_include_directories(anolis_provider_ezo_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(anolis_provider_ezo_support PUBLIC yaml-cpp::yaml-cpp)
target_compile_definitions(anolis_provider_ezo_support PUBLIC
    ANOLIS_PROVIDER_EZO_VERSION="${PROJECT_VERSION}")
anolis_provider_ezo_apply_warnings(anolis_provider_ezo_support)

add_library(anolis_provider_ezo_core STATIC
    src/core/ezo_provider_runtime.cpp
    src/core/runtime_state.cpp
    src/devices/common/sample_helpers.cpp
    src/devices/common/device_adapter.cpp
    src/devices/ph/ph_adapter.cpp
    src/devices/orp/orp_adapter.cpp
    src/devices/ec/ec_adapter.cpp
    src/devices/do/do_adapter.cpp
    src/devices/rtd/rtd_adapter.cpp
    src/devices/hum/hum_adapter.cpp
    src/i2c/bus_executor.cpp
    src/i2c/ezo_i2c_bridge.cpp
    src/i2c/session.cpp)
target_include_directories(anolis_provider_ezo_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(anolis_provider_ezo_core
    PUBLIC
        anolis::provider_sdk
        anolis_provider_ezo_support
        protobuf::libprotobuf
        Threads::Threads
        ezo::ezo_core)
target_compile_definitions(anolis_provider_ezo_core PUBLIC
    ANOLIS_PROVIDER_EZO_VERSION="${PROJECT_VERSION}")
anolis_provider_ezo_apply_warnings(anolis_provider_ezo_core)

add_executable(anolis-provider-ezo src/core/main.cpp)
target_include_directories(anolis-provider-ezo PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(anolis-provider-ezo PRIVATE anolis_provider_ezo_core)
target_compile_definitions(anolis-provider-ezo PRIVATE
    ANOLIS_PROVIDER_EZO_VERSION="${PROJECT_VERSION}")
anolis_provider_ezo_apply_warnings(anolis-provider-ezo)

if(WIN32)
    target_compile_definitions(anolis_provider_ezo_support PUBLIC _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(anolis_provider_ezo_core PUBLIC _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(anolis-provider-ezo PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(BUILD_TESTING)
    find_package(GTest CONFIG REQUIRED)
    add_subdirectory(tests/unit)
endif()
