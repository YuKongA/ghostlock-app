include_guard(GLOBAL)

# Purpose: build remote/main's untouched 43499 translation units as the app's
# standalone exec payload (target `glued43499_exec`, OUTPUT_NAME `43499-Glued`).
# The payload stays an executable ELF; making it SHARED would change attack semantics.
# Inputs are the explicitly selected upstream TU set; output is the lib-prefixed
# executable AGP packages into nativeLibraryDir.
# Failure: CMake configuration or the NDK compile/link fails the variant build.
# Preconditions: included by backends/CMakeLists.txt when GHOSTLOCK_BACKEND_43499_GLUED=ON;
# AGP supplies the Android arm64 NDK toolchain and output dir.
# Phase: application native build; this target is never run by CMake or Gradle.
# Note: included via include(), so relative source paths would resolve against the cpp/
# root; every path is anchored at ${CMAKE_CURRENT_LIST_DIR}.

add_executable(glued43499_exec
    ${CMAKE_CURRENT_LIST_DIR}/main.cpp
    ${CMAKE_CURRENT_LIST_DIR}/attack/ops.cpp
    ${CMAKE_CURRENT_LIST_DIR}/profile/entry.cpp
    ${CMAKE_CURRENT_LIST_DIR}/memory/address_space.cpp
    ${CMAKE_CURRENT_LIST_DIR}/memory/heap_context.cpp
    ${CMAKE_CURRENT_LIST_DIR}/race/pi_race.cpp
    ${CMAKE_CURRENT_LIST_DIR}/route/route_controller.cpp
    ${CMAKE_CURRENT_LIST_DIR}/route/route_middleware.cpp
    ${CMAKE_CURRENT_LIST_DIR}/race/threads.cpp
    ${CMAKE_CURRENT_LIST_DIR}/route/tcp_zerocopy_route.cpp
    ${CMAKE_CURRENT_LIST_DIR}/route/select_stack_route.cpp
    ${CMAKE_CURRENT_LIST_DIR}/route/multicast_waiter_route.cpp
    ${CMAKE_CURRENT_LIST_DIR}/memory/payload_builder.cpp
    ${CMAKE_CURRENT_LIST_DIR}/session/runtime_config.cpp
    ${CMAKE_CURRENT_LIST_DIR}/profile/binary.cpp
    ${CMAKE_CURRENT_LIST_DIR}/support/util.cpp
    ${CMAKE_CURRENT_LIST_DIR}/session/exploit_session.cpp
    ${CMAKE_CURRENT_LIST_DIR}/session/root_child_frontend.cpp
    ${CMAKE_CURRENT_LIST_DIR}/session/backend/cve_2026_43499_backend.cpp
    ${CMAKE_CURRENT_LIST_DIR}/session/handoff_probe.cpp
    ${CMAKE_CURRENT_LIST_DIR}/session/victim_process.cpp
    ${CMAKE_CURRENT_LIST_DIR}/session/victim_context.cpp
    ${CMAKE_CURRENT_LIST_DIR}/support/native_resource.cpp
    ${CMAKE_CURRENT_LIST_DIR}/support/run_state.cpp
)

target_include_directories(glued43499_exec PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
)
target_compile_definitions(glued43499_exec PRIVATE
    "TARGET_CONFIG_H=\"kernel/target.h\""
)
target_compile_options(glued43499_exec PRIVATE
    -O2
    -flto
    -Wall
    -Wextra
    -Wconversion
    -Wsign-conversion
    -Wno-unused-parameter
    -Wno-sign-compare
    -Wno-unused-function
    -pthread
)
set_target_properties(glued43499_exec PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS ON
    PREFIX "lib"
    SUFFIX ".so"
    OUTPUT_NAME "43499-Glued"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
)
target_link_options(glued43499_exec PRIVATE
    -fPIE
    -pie
    -pthread
    -flto
    -static-libstdc++
    -Wl,-z,max-page-size=16384
)
