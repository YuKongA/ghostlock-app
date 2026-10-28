cmake_minimum_required(VERSION 3.22)

# Android app-owned native build. All native sources live under this directory.
project(ghostlock_native LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(gl_common STATIC
        common/gl_log.c
)
target_include_directories(gl_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(gl_common PUBLIC log)

# general: Kotlin-only DSO (KernelSnitch discovery today).
# The scan worker threads, mmap'd shared state and 64 GiB futex arena only make
# sense in one dedicated DSO; the backend libraries must not carry a second copy.
add_library(ghostlock_general SHARED
    glgeneral/general_jni.c
)
target_include_directories(ghostlock_general PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/glgeneral
)
# kernelsnitch/*.h select code paths per architecture; Android arm64 is __ARM.
target_compile_definitions(ghostlock_general PRIVATE __ARM)
target_link_libraries(ghostlock_general PRIVATE gl_common log)
target_link_options(ghostlock_general PRIVATE -Wl,-z,max-page-size=16384)
set_target_properties(ghostlock_general PROPERTIES
    PREFIX "lib"
    SUFFIX ".so"
    OUTPUT_NAME ghostlock_general
    C_VISIBILITY_PRESET hidden
)

# 43284: DFRoot-derived standalone payload.
# AGP packages an executable named lib*.so into nativeLibraryDir; the app execs it.
set(GLK_43284_KO
    ${CMAKE_CURRENT_SOURCE_DIR}/gl43284/ko/dfroot-android12-5.10.ko
    ${CMAKE_CURRENT_SOURCE_DIR}/gl43284/ko/dfroot-android13-5.10.ko
    ${CMAKE_CURRENT_SOURCE_DIR}/gl43284/ko/dfroot-android13-5.15.ko
    ${CMAKE_CURRENT_SOURCE_DIR}/gl43284/ko/dfroot-android14-5.15.ko
    ${CMAKE_CURRENT_SOURCE_DIR}/gl43284/ko/dfroot-android14-6.1.ko
    ${CMAKE_CURRENT_SOURCE_DIR}/gl43284/ko/dfroot-android15-6.6.ko
    ${CMAKE_CURRENT_SOURCE_DIR}/gl43284/ko/dfroot-android16-6.12.ko
    ${CMAKE_CURRENT_SOURCE_DIR}/gl43284/ko/dfroot-android17-6.18.ko
)
add_library(ghostlock43284 SHARED
    gl43284/exp.c
    gl43284/libcxx.S
    gl43284/elf_parser.c
    gl43284/df_log.c
    gl43284/glk43284_api.c
    gl43284/jni_attach.c
    gl43284/jni_exploit.c
)
target_include_directories(ghostlock43284 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/gl43284
        ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_link_libraries(ghostlock43284 PRIVATE gl_common log)
target_link_options(ghostlock43284 PRIVATE -Wl,-z,max-page-size=16384)
set_target_properties(ghostlock43284 PROPERTIES
    PREFIX "lib"
    SUFFIX ".so"
    OUTPUT_NAME ghostlock43284
    C_VISIBILITY_PRESET hidden
)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/gl43284/exp.c PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/gl43284/splicehelper;${GLK_43284_KO}"
)

# 43499: backend-specific payload.
# The legacy C tree is parked under gl43499/{legacy,very_legacy}; the staged
# G4L sprint implements the C11 resource primitives under gl43499/core while
# `api_unavailable.c` keeps the not-yet-implemented symbols fail-closed.
# `__ARM` selects the arm64 branch of the header-only KernelSnitch detector.
add_library(ghostlock43499 SHARED
    gl43499/jni43499.c
    # A1 debug-only 单发 multicast probe（release 不编入/不导出）。
    $<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/gl43499/mcast510_probe.c>
    gl43499/core/placement.c
    gl43499/core/page.c
    gl43499/core/actor.c
    gl43499/core/carrier_multicast.c
    gl43499/core/carrier_pselect.c
    gl43499/core/carrier_tcp.c
    gl43499/core/perf.c
    gl43499/core/victim_protocol.c
)
target_include_directories(ghostlock43499 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/gl43499
    ${CMAKE_CURRENT_SOURCE_DIR}/glgeneral
)
target_compile_definitions(ghostlock43499 PRIVATE __ARM
    # E4 零 fork ①/严格 ②（task-60）：wrap 计数仅 Debug 编入；Release 无 wrap。
    $<$<CONFIG:Debug>:GLK_VICTIM_FORK_WRAP=1>
)
target_link_libraries(ghostlock43499 PRIVATE gl_common log)
target_link_options(ghostlock43499 PRIVATE -Wl,-z,max-page-size=16384
    # E4 零 fork ①：仅 Debug 链接期 wrap，计数由 victim_protocol.c 提供
    # （生产路径=0）；Release 不传 wrap ⇒ readelf 无 fork/vfork 导入（严格 ②）。
    $<$<CONFIG:Debug>:-Wl,--wrap=fork>
    $<$<CONFIG:Debug>:-Wl,--wrap=vfork>
)
set_target_properties(ghostlock43499 PROPERTIES
    PREFIX "lib"
    SUFFIX ".so"
    OUTPUT_NAME ghostlock43499
    C_VISIBILITY_PRESET hidden
)

# 通用 victim helper（实现批 A，task-52）：backend-agnostic PIE。
# AGP 只打包 lib*.so 形态的文件（见上方 dfroot 先例）；app 用 posix_spawn 从
# nativeLibraryDir 启动它（生产/JNI 路径零 fork）。helper 只承载机械能力：
# probeUid/perfSelfSample(仅 syscall+raw memfd)/execScript(posix_spawn)/park/bye。
add_executable(ghostlock_victim_helper
    gl43499/core/victim_helper/victim_helper.c
    gl43499/core/victim_protocol.c
)
target_include_directories(ghostlock_victim_helper PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/gl43499/core
)
target_compile_definitions(ghostlock_victim_helper PRIVATE
    # E4 零 fork ①/严格 ②（task-60）：helper 侧 wrap 计数仅 Debug。
    $<$<CONFIG:Debug>:GLK_VICTIM_FORK_WRAP=1>
)
target_link_options(ghostlock_victim_helper PRIVATE -Wl,-z,max-page-size=16384
    # E4 零 fork ①（helper 侧计数随 bye 回执上报）；Release 不传 wrap。
    $<$<CONFIG:Debug>:-Wl,--wrap=fork>
    $<$<CONFIG:Debug>:-Wl,--wrap=vfork>
)
set_target_properties(ghostlock_victim_helper PROPERTIES
    PREFIX "lib"
    SUFFIX ".so"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
)

# 98260：上游 timer exploit 的独立 payload（GhostLock port，B1）。
# 形态 = 静态 ET_EXEC：上游语义依赖 `/proc/self/exe` 重入自身（child `wait <mode>`
# 与 target `argv[0]==""` 的 exec 成功信号）以及"失败即 park、重启唯一清理"的
# 独立进程生命周期，in-process 无法等价。AGP 按 `lib*.so` 收集进 nativeLibraryDir，
# 由 Shizuku UserService 侧 JNI posix_spawn 启动（D2 delta）；诊断 mode 仅 Debug 编入。
add_executable(ghostlock98260_helper
    gl98260/timer.c
    gl98260/params.c
)
target_include_directories(ghostlock98260_helper PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/gl98260
)
target_compile_options(ghostlock98260_helper PRIVATE
    -Wall -Wextra -Werror -pthread
)
target_link_options(ghostlock98260_helper PRIVATE
    -static
    -pthread
    -Wl,--build-id=none
    -Wl,-z,norelro
    -Wl,-z,max-page-size=16384
)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(ghostlock98260_helper PRIVATE GLK98260_ENABLE_DIAGNOSTIC=1)
endif()
set_target_properties(ghostlock98260_helper PROPERTIES
    PREFIX "lib"
    SUFFIX ".so"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
)

# 98260：JNI 机械面（B2；在 Shizuku UserService 进程内加载）。
# 只暴露 spawnStatic/waitExit；无 kill/signal/fork；不用 --wrap=fork（该证明只属 43499 app-JVM 路径）。
add_library(ghostlock98260jni SHARED
    gl98260/jni98260.c
)
target_include_directories(ghostlock98260jni PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/gl98260
)
target_compile_options(ghostlock98260jni PRIVATE
    -Wall -Wextra -Werror
)
target_link_libraries(ghostlock98260jni PRIVATE gl_common log)
target_link_options(ghostlock98260jni PRIVATE -Wl,-z,max-page-size=16384)
set_target_properties(ghostlock98260jni PROPERTIES
    PREFIX "lib"
    SUFFIX ".so"
    OUTPUT_NAME ghostlock98260jni
    C_VISIBILITY_PRESET hidden
)
