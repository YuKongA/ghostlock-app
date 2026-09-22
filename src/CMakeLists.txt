# ONLY FOR IDE SYNTAX ANALYSIS AND AUTO COMPLETION
cmake_minimum_required(VERSION 4.3)
project(src LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CLion-only configuration. The native binary is built by the parent Gradle
# task (aarch64-linux-android / arm64-v8a), not by this CMake project.
set(CMAKE_C_COMPILER_TARGET aarch64-linux-android CACHE STRING
        "Android compiler target used for CLion code completion")
set(CMAKE_SYSTEM_NAME Android CACHE STRING
        "Target platform used for CLion code completion")
set(CMAKE_ANDROID_ARCH_ABI arm64-v8a CACHE STRING
        "Android ABI used for CLion code completion")
set(CMAKE_ANDROID_API 34 CACHE STRING
        "Android API level used for CLion code completion")

# Resolve the NDK used by the parent Gradle build so CLion can index Android
# headers (linux/*.h, android/*.h, and the NDK toolchain headers).
set(ANDROID_NDK_DIR "${CMAKE_ANDROID_NDK}")
if(NOT ANDROID_NDK_DIR)
    set(ANDROID_NDK_DIR "$ENV{ANDROID_NDK_HOME}")
endif()
if(NOT ANDROID_NDK_DIR)
    set(ANDROID_NDK_DIR "$ENV{ANDROID_NDK_ROOT}")
endif()
if(NOT ANDROID_NDK_DIR)
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/../local.properties" LOCAL_PROPERTIES)
    foreach(PROPERTY IN LISTS LOCAL_PROPERTIES)
        if(PROPERTY MATCHES "^ndk\.dir=(.*)$")
            set(ANDROID_NDK_DIR "${CMAKE_MATCH_1}")
            break()
        endif()
    endforeach()
endif()
if(ANDROID_NDK_DIR)
    file(TO_CMAKE_PATH "${ANDROID_NDK_DIR}" ANDROID_NDK_DIR)
    set(ANDROID_NDK_SYSROOT "${ANDROID_NDK_DIR}/toolchains/llvm/prebuilt")
    file(GLOB NDK_PREBUILTS LIST_DIRECTORIES true "${ANDROID_NDK_SYSROOT}/*")
    if(NDK_PREBUILTS)
        list(GET NDK_PREBUILTS 0 NDK_PREBUILT)
        list(APPEND NDK_INCLUDE_DIRS
                "${NDK_PREBUILT}/sysroot/usr/include"
                "${NDK_PREBUILT}/sysroot/usr/include/aarch64-linux-android")
    endif()
endif()

set(SOURCES
        core/main.cpp
        core/exploit_ops.cpp
        core/profile_entry.cpp
        core/legacy_support/legacy_profile_converter.cpp
        core/legacy_support/legacy_entrypoint_starter.cpp
        core/memory/address_space.cpp
        core/memory/heap_context.cpp
        core/pi_race.cpp
        core/routes/route_controller.cpp
        core/routes/route_threads.cpp
        core/routes/tcp_zerocopy_route.cpp
        core/routes/select_stack_route.cpp
        core/routes/multicast_waiter_route.cpp
        core/memory/payload_builder.cpp
        core/session/runtime_config.cpp
        core/offsets_json.cpp
        core/util.cpp
        core/routes/route_operations.cpp
        core/session/exploit_session.cpp
        core/session/exploit_stages.cpp
        core/session/handoff_probe.cpp
        core/session/victim_process.cpp
        core/session/victim_context.cpp
        core/support/native_resource.cpp
)

add_executable(src ${SOURCES})

# Mirror the Makefile warning policy so IDE diagnostics match the native build.
target_compile_options(src PRIVATE
        -fno-rtti
        -Wall -Wextra -Wconversion -Wsign-conversion
        -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function)

target_include_directories(src PRIVATE
        core
        core/memory
        core/routes
        core/session
        core/support
        core/kernelsnitch
        kernels
        ${NDK_INCLUDE_DIRS}
)
