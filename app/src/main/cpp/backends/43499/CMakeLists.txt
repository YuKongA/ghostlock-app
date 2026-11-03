include_guard(GLOBAL)

# Purpose: original CVE-2026-43499 backend chunk. Defines the in-process JNI payload
# DSO (ghostlock43499) and the backend-agnostic victim helper executable
# (ghostlock_victim_helper); both target/output names are unchanged from the pre-chunk
# top-level build file.
# Inputs: jni43499.c, the core sources (actor/page/carrier/perf/placement, victim
# protocol) and the debug-only multicast probe source.
# Output: libghostlock43499.so and libghostlock_victim_helper.so in AGP's native output
# directory when GHOSTLOCK_BACKEND_43499=ON.
# Failure: compile/link errors fail the variant build.
# Preconditions: included by backends/CMakeLists.txt only when GHOSTLOCK_BACKEND_43499=ON;
# gl_common and the glgeneral include root already exist.
# Phase: application native build; the helper is executed on devices at runtime.
# Note: included via include(), so every path is anchored at ${CMAKE_CURRENT_LIST_DIR}.

# 43499: backend-specific payload.
# The legacy C tree is parked under legacy/ and very_legacy/; the staged G4L sprint
# implements the C11 resource primitives under core/ while `api_unavailable.c` keeps
# the not-yet-implemented symbols fail-closed.
# `__ARM` selects the arm64 branch of the header-only KernelSnitch detector.
add_library(ghostlock43499 SHARED
    ${CMAKE_CURRENT_LIST_DIR}/jni43499.c
    # A1 debug-only 单发 multicast probe（release 不编入/不导出）。
    $<$<CONFIG:Debug>:${CMAKE_CURRENT_LIST_DIR}/mcast510_probe.c>
    ${CMAKE_CURRENT_LIST_DIR}/core/placement.c
    ${CMAKE_CURRENT_LIST_DIR}/core/page.c
    ${CMAKE_CURRENT_LIST_DIR}/core/actor.c
    ${CMAKE_CURRENT_LIST_DIR}/core/carrier_multicast.c
    ${CMAKE_CURRENT_LIST_DIR}/core/carrier_pselect.c
    ${CMAKE_CURRENT_LIST_DIR}/core/carrier_tcp.c
    ${CMAKE_CURRENT_LIST_DIR}/core/perf.c
    ${CMAKE_CURRENT_LIST_DIR}/core/victim_protocol.c
)
target_include_directories(ghostlock43499 PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../../glgeneral
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
# AGP 只打包 lib*.so 形态的文件（见 glued/dfroot 先例）；app 用 posix_spawn 从
# nativeLibraryDir 启动它（生产/JNI 路径零 fork）。helper 只承载机械能力：
# probeUid/perfSelfSample(仅 syscall+raw memfd)/execScript(posix_spawn)/park/bye。
add_executable(ghostlock_victim_helper
    ${CMAKE_CURRENT_LIST_DIR}/core/victim_helper/victim_helper.c
    ${CMAKE_CURRENT_LIST_DIR}/core/victim_protocol.c
)
target_include_directories(ghostlock_victim_helper PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/core
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
