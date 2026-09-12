# ONLY FOR IDE SYNTAX ANALYSIS AND AUTO COMPLETION
cmake_minimum_required(VERSION 4.3)
project(src C)

set(CMAKE_C_STANDARD 11)

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

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS
        core/*.c
        core/*.h
        kernels/*.c
        kernels/*.h
)

add_executable(src ${SOURCES})

target_include_directories(src PRIVATE
        core
        core/kernelsnitch
        kernels
        ${NDK_INCLUDE_DIRS}
)
