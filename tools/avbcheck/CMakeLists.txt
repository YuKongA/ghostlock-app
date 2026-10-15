# tools/avbcheck：迁移自同目录的 Makefile（2026-10-15）
#
#  主机目标 avbcheck-host    —— 离线核对用（原 make host）
#  设备目标 avbcheck-device  —— NDK aarch64 静态可执行（原 make，由 Gradle 任务以工具链文件驱动）
#  单测   avbcheck-tests     —— 主机单测（原 make test）
#
# 源与旗标照原 Makefile：src/avb.cpp src/main.cpp src/sha2.cpp；-std=c++17 -O2 -Wall -Wextra
# -ffunction-sections -fdata-sections；单测另加 -Isrc 且用 tests/test_avbcheck.cpp。
#
# 不再硬编码任何本机 NDK 路径：NDK 由 Gradle 侧统一解析（见根 build.gradle.kts 的 NDK 解析写法）。
cmake_minimum_required(VERSION 3.22)
project(avbcheck CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(AVBCHECK_COMMON_OPTS -O2 -Wall -Wextra -ffunction-sections -fdata-sections)
set(AVBCHECK_SOURCES src/avb.cpp src/main.cpp src/sha2.cpp)

add_executable(avbcheck-host ${AVBCHECK_SOURCES})
target_include_directories(avbcheck-host PRIVATE src)
target_compile_options(avbcheck-host PRIVATE ${AVBCHECK_COMMON_OPTS})

add_executable(avbcheck-device ${AVBCHECK_SOURCES})
target_include_directories(avbcheck-device PRIVATE src)
target_compile_options(avbcheck-device PRIVATE ${AVBCHECK_COMMON_OPTS})
set_target_properties(avbcheck-device PROPERTIES LINK_FLAGS -static)

enable_testing()
add_executable(avbcheck-tests src/avb.cpp src/sha2.cpp tests/test_avbcheck.cpp)
target_include_directories(avbcheck-tests PRIVATE src)
target_compile_options(avbcheck-tests PRIVATE ${AVBCHECK_COMMON_OPTS})
add_test(NAME avbcheck-tests COMMAND avbcheck-tests)
