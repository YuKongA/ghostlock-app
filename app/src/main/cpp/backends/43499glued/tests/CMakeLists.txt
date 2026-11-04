cmake_minimum_required(VERSION 3.22)

# Purpose: build every current, host-compatible 43499-Glued test from the
# production translation-unit boundaries that each test actually exercises.
# Inputs: 26 top-level test translation units, one host data-flow harness, the
# mapped production/stub sources, and the repository disposition authority.
# Output: 27 independently named CTest executables in this binary tree.
# Failure: source/registry drift, compiler warning, compile/link failure, or a
# failed assertion stops the owning Gradle module task.
# Preconditions: configured on a POSIX desktop by nativeTest43499GluedHost.
# Phase: D7.3+D7.4-N current-tree native verification.

project(ghostlock_43499glued_host_tests LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
enable_testing()

find_package(Threads REQUIRED)

get_filename_component(GL_MODULE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
get_filename_component(GL_REPO_ROOT "${GL_MODULE_ROOT}/../../../../../.." ABSOLUTE)
set(GL_DISPOSITION "${GL_REPO_ROOT}/tools/tests/native-test-disposition.tsv")
set(GL_HOST_WARNINGS
    -Wall
    -Wextra
    -Wconversion
    -Wsign-conversion
    -Werror
    -UNDEBUG
)
set(GL_REGISTERED_TEST_SOURCES "")

# Purpose: register one 43499-Glued host executable and its CTest entry.
# Parameters: target is the stable CTest name; TEST_SOURCE is the single
# candidate owned by the disposition table; SOURCES are production/stub TUs.
# Return: appends the repo-relative TEST_SOURCE to the module registry.
# Failure: a missing argument or any strict compile/link error fails the build.
# Preconditions: all paths after TEST_SOURCE are relative to GL_MODULE_ROOT.
# Phase: host native verification.
function(gl_add_43499_host_test target)
    set(options DEAD_STRIP TARGET_CONFIG THREADS)
    set(one_value_args TEST_SOURCE)
    set(multi_value_args SOURCES INCLUDE_DIRECTORIES)
    cmake_parse_arguments(GL_TEST
        "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})
    if(NOT GL_TEST_TEST_SOURCE)
        message(FATAL_ERROR "${target}: TEST_SOURCE is required")
    endif()

    set(target_sources "${GL_MODULE_ROOT}/${GL_TEST_TEST_SOURCE}")
    foreach(source IN LISTS GL_TEST_SOURCES)
        list(APPEND target_sources "${GL_MODULE_ROOT}/${source}")
    endforeach()
    add_executable(${target} ${target_sources})
    target_include_directories(${target} BEFORE PRIVATE
        ${GL_TEST_INCLUDE_DIRECTORIES}
        "${GL_MODULE_ROOT}"
    )
    if(GL_TEST_TARGET_CONFIG)
        target_compile_definitions(${target} PRIVATE
            "TARGET_CONFIG_H=\"kernel/target.h\""
        )
    endif()
    target_compile_options(${target} PRIVATE ${GL_HOST_WARNINGS})
    if(GL_TEST_THREADS)
        target_link_libraries(${target} PRIVATE Threads::Threads)
    endif()
    if(GL_TEST_DEAD_STRIP)
        target_compile_options(${target} PRIVATE
            -ffunction-sections
            -fdata-sections
        )
        if(APPLE)
            target_link_options(${target} PRIVATE -Wl,-dead_strip)
        else()
            target_link_options(${target} PRIVATE -Wl,--gc-sections)
        endif()
    endif()
    add_test(NAME ${target} COMMAND ${target})

    set(repo_relative
        "app/src/main/cpp/backends/43499glued/${GL_TEST_TEST_SOURCE}")
    list(APPEND GL_REGISTERED_TEST_SOURCES "${repo_relative}")
    set(GL_REGISTERED_TEST_SOURCES
        "${GL_REGISTERED_TEST_SOURCES}" PARENT_SCOPE)
endfunction()

gl_add_43499_host_test(address_space_test
    TEST_SOURCE tests/address_space_test.cpp
    SOURCES memory/address_space.cpp)
gl_add_43499_host_test(backend_contract_test
    TEST_SOURCE tests/backend_contract_test.cpp)
gl_add_43499_host_test(component_catalog_test
    TEST_SOURCE tests/component_catalog_test.cpp)
gl_add_43499_host_test(cpp_link_probe_test
    TEST_SOURCE tests/cpp_link_probe_test.c
    SOURCES tests/cpp_link_probe.cpp)
set_property(TARGET cpp_link_probe_test PROPERTY LINKER_LANGUAGE CXX)
gl_add_43499_host_test(futex_hash_test
    TEST_SOURCE tests/futex_hash_test.cpp)
gl_add_43499_host_test(handoff_probe_test
    THREADS
    TEST_SOURCE tests/handoff_probe_test.cpp
    SOURCES session/handoff_probe.cpp support/native_resource.cpp)
gl_add_43499_host_test(heap_context_test
    THREADS
    TEST_SOURCE tests/heap_context_test.cpp
    SOURCES memory/heap_context.cpp support/native_resource.cpp)
gl_add_43499_host_test(kernelsnitch_scan_bounds_test
    TEST_SOURCE tests/kernelsnitch_scan_bounds_test.cpp)
gl_add_43499_host_test(native_resource_test
    THREADS
    TEST_SOURCE tests/native_resource_test.cpp
    SOURCES support/native_resource.cpp)
gl_add_43499_host_test(number_parse_test
    TEST_SOURCE tests/number_parse_test.cpp)
gl_add_43499_host_test(payload_builder_test
    TEST_SOURCE tests/payload_builder_test.cpp
    SOURCES memory/payload_builder.cpp)
gl_add_43499_host_test(pi_race_test
    THREADS
    TEST_SOURCE tests/pi_race_test.cpp
    SOURCES race/pi_race.cpp support/native_resource.cpp)
gl_add_43499_host_test(profile_binary_test
    TEST_SOURCE tests/profile_binary_test.cpp
    SOURCES profile/binary.cpp)
# profile/binary.cpp deliberately names an offset argument inside its PLAIN
# table macro; the Android target already suppresses this legacy diagnostic.
target_compile_options(profile_binary_test PRIVATE -Wno-unused-parameter)
gl_add_43499_host_test(profile_test
    TEST_SOURCE tests/profile_test.cpp)
gl_add_43499_host_test(route_catalog_test
    TEST_SOURCE tests/route_catalog_test.cpp)
gl_add_43499_host_test(route_controller_test
    DEAD_STRIP THREADS
    TEST_SOURCE tests/route_controller_test.cpp
    SOURCES route/route_controller.cpp support/native_resource.cpp)
gl_add_43499_host_test(route_lifecycle_test
    TEST_SOURCE tests/route_lifecycle_test.cpp)
gl_add_43499_host_test(route_policy_test
    DEAD_STRIP
    TEST_SOURCE tests/route_policy_test.cpp)
gl_add_43499_host_test(route_status_test
    TEST_SOURCE tests/route_status_test.cpp)
gl_add_43499_host_test(run_state_test
    TEST_SOURCE tests/run_state_test.cpp
    SOURCES support/run_state.cpp)
gl_add_43499_host_test(runtime_paths_test
    TEST_SOURCE tests/runtime_paths_test.cpp)
gl_add_43499_host_test(runtime_time_test
    TEST_SOURCE tests/runtime_time_test.cpp)
gl_add_43499_host_test(select_stack_route_test
    THREADS
    TEST_SOURCE tests/select_stack_route_test.cpp
    SOURCES
        route/select_stack_route.cpp
        race/pi_race.cpp
        support/native_resource.cpp)
gl_add_43499_host_test(target_constants_test
    TEST_SOURCE tests/target_constants_test.cpp)
gl_add_43499_host_test(tcp_zerocopy_route_test
    THREADS
    TEST_SOURCE tests/tcp_zerocopy_route_test.cpp
    SOURCES
        route/tcp_zerocopy_route.cpp
        race/pi_race.cpp
        support/native_resource.cpp)
gl_add_43499_host_test(victim_context_test
    THREADS
    TEST_SOURCE tests/victim_context_test.cpp
    SOURCES session/victim_context.cpp support/native_resource.cpp)

# The host harness deliberately shadows Linux-only headers before the module
# root and substitutes every external side-effect boundary with a local stub.
gl_add_43499_host_test(backend_dataflow_test
    TARGET_CONFIG THREADS
    TEST_SOURCE tests/host/backend_dataflow_test.cpp
    INCLUDE_DIRECTORIES
        "${GL_MODULE_ROOT}/tests/host/include"
        "${GL_MODULE_ROOT}/tests/host"
    SOURCES
        tests/host/host_attack_script.cpp
        tests/host/attack_stub.cpp
        tests/host/support_stub.cpp
        tests/host/route_stub.cpp
        tests/host/victim_stub.cpp
        tests/host/handoff_stub.cpp
        tests/host/glue_stub.cpp
        session/backend/cve_2026_43499_backend.cpp
        session/root_child_frontend.cpp
        session/exploit_session.cpp
        session/runtime_config.cpp
        memory/heap_context.cpp
        memory/address_space.cpp
        race/pi_race.cpp
        support/native_resource.cpp
        support/run_state.cpp)
# The host stub chain imports the legacy kernelsnitch utility header. Match the
# production target's two existing suppressions without weakening other tests.
target_compile_options(backend_dataflow_test PRIVATE
    -Wno-sign-conversion
    -Wno-unused-function
)

# The module owns every test-shaped source below its tests/ tree. A new test
# cannot silently land outside CTest or appear twice in the explicit registry.
file(GLOB_RECURSE GL_DISCOVERED_TEST_FILES CONFIGURE_DEPENDS
    "${GL_MODULE_ROOT}/tests/*_test.c"
    "${GL_MODULE_ROOT}/tests/*_test.cc"
    "${GL_MODULE_ROOT}/tests/*_test.cpp"
    "${GL_MODULE_ROOT}/tests/test_*.c"
    "${GL_MODULE_ROOT}/tests/test_*.cc"
    "${GL_MODULE_ROOT}/tests/test_*.cpp"
)
set(GL_DISCOVERED_TEST_SOURCES "")
foreach(source IN LISTS GL_DISCOVERED_TEST_FILES)
    file(RELATIVE_PATH repo_relative "${GL_REPO_ROOT}" "${source}")
    list(APPEND GL_DISCOVERED_TEST_SOURCES "${repo_relative}")
endforeach()
list(REMOVE_DUPLICATES GL_DISCOVERED_TEST_SOURCES)

list(LENGTH GL_REGISTERED_TEST_SOURCES registered_count)
set(GL_UNIQUE_REGISTERED_TEST_SOURCES ${GL_REGISTERED_TEST_SOURCES})
list(REMOVE_DUPLICATES GL_UNIQUE_REGISTERED_TEST_SOURCES)
list(LENGTH GL_UNIQUE_REGISTERED_TEST_SOURCES unique_registered_count)
if(NOT registered_count EQUAL unique_registered_count)
    message(FATAL_ERROR
        "43499glued duplicate test registration: ${GL_REGISTERED_TEST_SOURCES}")
endif()
list(SORT GL_DISCOVERED_TEST_SOURCES)
list(SORT GL_UNIQUE_REGISTERED_TEST_SOURCES)
if(NOT "${GL_DISCOVERED_TEST_SOURCES}" STREQUAL
       "${GL_UNIQUE_REGISTERED_TEST_SOURCES}")
    message(FATAL_ERROR
        "43499glued module discovery drift: discovered=${GL_DISCOVERED_TEST_SOURCES}; "
        "registered=${GL_UNIQUE_REGISTERED_TEST_SOURCES}")
endif()

# Fail closed when the module registry and machine disposition diverge. The
# root inventory separately proves that the disposition covers all candidates.
if(NOT EXISTS "${GL_DISPOSITION}")
    message(FATAL_ERROR "native test disposition is missing: ${GL_DISPOSITION}")
endif()
file(STRINGS "${GL_DISPOSITION}" GL_DISPOSITION_LINES)
set(GL_EXPECTED_TEST_SOURCES "")
foreach(line IN LISTS GL_DISPOSITION_LINES)
    if(line STREQUAL "" OR line MATCHES "^relative_path\\t")
        continue()
    endif()
    string(REPLACE "\t" ";" fields "${line}")
    list(LENGTH fields field_count)
    if(NOT field_count EQUAL 4)
        message(FATAL_ERROR "invalid disposition row: ${line}")
    endif()
    list(GET fields 0 source)
    list(GET fields 1 status)
    list(GET fields 2 module)
    if(status STREQUAL "host-ctest" AND module STREQUAL "43499glued")
        list(APPEND GL_EXPECTED_TEST_SOURCES "${source}")
    endif()
endforeach()
list(SORT GL_EXPECTED_TEST_SOURCES)
list(SORT GL_REGISTERED_TEST_SOURCES)
if(NOT "${GL_EXPECTED_TEST_SOURCES}" STREQUAL
       "${GL_REGISTERED_TEST_SOURCES}")
    message(FATAL_ERROR
        "43499glued registry drift: disposition=${GL_EXPECTED_TEST_SOURCES}; "
        "cmake=${GL_REGISTERED_TEST_SOURCES}")
endif()
