include_guard(GLOBAL)

# Purpose: CVE-2026-98260 exact-profile helper-only experiment chunk.
# Inputs: timer/timer.c or the isolated core_state probe selected at configure
# time, plus one compile-time target parameter authority.
# Output: static ET_EXEC packaged as libghostlock98260_helper.so.
# Failure: an unknown target, incompatible probe, or missing source fails configure.
# Preconditions: included by backends/CMakeLists.txt only when GHOSTLOCK_BACKEND_98260=ON.
# Phase: staged ids -> leak -> real-cred -> fake/KSU device experiment.
# Note: included via include(), so every path is anchored at ${CMAKE_CURRENT_LIST_DIR}.

set(GLK98260_TARGET_PROFILE "AVD_61" CACHE STRING
    "Exact CVE-2026-98260 target profile")
set_property(CACHE GLK98260_TARGET_PROFILE PROPERTY STRINGS
    AVD_61
    A301SO_515
)
if(GLK98260_TARGET_PROFILE STREQUAL "AVD_61")
    set(GLK_98260_TARGET_DEFINITION GLK98260_TARGET_AVD_61=1)
    set(GLK_98260_TARGET_HEADER timer/avd_61_params.h)
elseif(GLK98260_TARGET_PROFILE STREQUAL "A301SO_515")
    set(GLK_98260_TARGET_DEFINITION GLK98260_TARGET_A301SO_515=1)
    set(GLK_98260_TARGET_HEADER timer/a301so_515_params.h)
else()
    message(FATAL_ERROR
        "Unknown GLK98260_TARGET_PROFILE=${GLK98260_TARGET_PROFILE}; "
        "expected AVD_61 or A301SO_515")
endif()

# The diagnostic selector preserves the existing target/artifact and process
# route. ON replaces only the translation unit with a no-timer core_state probe;
# it never links both entry points and cannot reach the stale-node plant.
option(GLK98260_CORE_STATE_PROBE
    "Build the isolated no-timer exec/core_state probe"
    OFF)
if(GLK98260_CORE_STATE_PROBE AND
   NOT GLK98260_TARGET_PROFILE STREQUAL "A301SO_515")
    message(FATAL_ERROR
        "GLK98260_CORE_STATE_PROBE is an A301SO_515-only diagnostic")
endif()

set(GLK_98260_REQUIRED_SOURCES
    timer/timer.c
    timer/target_params.h
    ${GLK_98260_TARGET_HEADER}
)
if(GLK98260_CORE_STATE_PROBE)
    list(APPEND GLK_98260_REQUIRED_SOURCES
        timer/core_state_probe.c
        timer/core_state_probe_params.h
    )
endif()
set(GLK_98260_MISSING_SOURCES "")
foreach(source IN LISTS GLK_98260_REQUIRED_SOURCES)
    if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/${source}")
        list(APPEND GLK_98260_MISSING_SOURCES "${source}")
    endif()
endforeach()
if(GLK_98260_MISSING_SOURCES)
    message(FATAL_ERROR
        "GHOSTLOCK_BACKEND_98260=ON but native sources are missing: "
        "${GLK_98260_MISSING_SOURCES} (expected under backends/98260/). "
        "Restore the 98260 sources or keep GHOSTLOCK_BACKEND_98260=OFF.")
endif()

if(GLK98260_CORE_STATE_PROBE)
    set(GLK_98260_ENTRY_SOURCE timer/core_state_probe.c)
else()
    set(GLK_98260_ENTRY_SOURCE timer/timer.c)
endif()

# 98260：上游 timer exploit 的独立 payload（GhostLock port，B1）。
# 形态 = 静态 ET_EXEC：上游语义依赖 `/proc/self/exe` 重入自身（child `wait <mode>`
# 与 target `argv[0]==""` 的 exec 成功信号）以及"失败即 park、重启唯一清理"的
# 独立进程生命周期，in-process 无法等价。AGP 按 `lib*.so` 收集进 nativeLibraryDir，
# 由 Shizuku UserService 侧 JNI posix_spawn 启动（D2 delta）；诊断 mode 仅 Debug 编入。
add_executable(ghostlock98260_helper
    ${CMAKE_CURRENT_LIST_DIR}/${GLK_98260_ENTRY_SOURCE}
)
target_include_directories(ghostlock98260_helper PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/timer
)
target_compile_options(ghostlock98260_helper PRIVATE
    -Wall -Wextra -Werror -pthread
)
target_compile_definitions(ghostlock98260_helper PRIVATE
    ${GLK_98260_TARGET_DEFINITION}
)
target_link_options(ghostlock98260_helper PRIVATE
    -static
    -pthread
    -Wl,--build-id=none
    -Wl,-z,norelro
    -Wl,-z,max-page-size=16384
)
set_target_properties(ghostlock98260_helper PROPERTIES
    PREFIX "lib"
    SUFFIX ".so"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
)

# JNI/App route is intentionally not declared in this experiment batch: its source and
# process-launch contract are absent. This target proves only the standalone helper path.
