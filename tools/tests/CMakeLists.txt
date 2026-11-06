cmake_minimum_required(VERSION 3.22)

# Purpose: build the current repository-wide, host-compatible native contract
# tests without importing Android/JNI production targets.
# Inputs: one explicitly registered C test plus the machine-owned disposition
# table at native-test-disposition.tsv.
# Output: one CTest executable in this CMake binary tree.
# Failure: missing/drifted disposition rows, compiler warnings, build failures,
# or assertion failures stop the owning Gradle task.
# Preconditions: configured by nativeTestContractsHost on a POSIX desktop host.
# Phase: D7.3+D7.4-N current-tree native verification.

project(ghostlock_native_contract_host_tests LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
enable_testing()

get_filename_component(GL_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(GL_COMMON_ROOT "${GL_REPO_ROOT}/app/src/main/cpp")
set(GL_DISPOSITION
    "${CMAKE_CURRENT_LIST_DIR}/native-test-disposition.tsv")
set(GL_HOST_WARNINGS
    -Wall
    -Wextra
    -Wconversion
    -Wsign-conversion
    -Werror
    -UNDEBUG
)

set(GL_REGISTERED_CONTRACT_SOURCES "")

# Purpose: register one pure C host contract and its CTest entry.
# Parameters: target is the stable test name; source is repo-relative.
# Return: appends source to the module registration set in the caller scope.
# Failure: CMake or the strict compiler flags fail configuration/build.
# Preconditions: source includes only host-compatible APIs.
# Phase: host native verification.
function(gl_add_contract_host_test target source)
    add_executable(${target} "${GL_REPO_ROOT}/${source}")
    target_compile_options(${target} PRIVATE ${GL_HOST_WARNINGS})
    add_test(NAME ${target} COMMAND ${target})
    list(APPEND GL_REGISTERED_CONTRACT_SOURCES "${source}")
    set(GL_REGISTERED_CONTRACT_SOURCES
        "${GL_REGISTERED_CONTRACT_SOURCES}" PARENT_SCOPE)
endfunction()

# gl_log.c must call the test seam while the test TU must retain libc write().
# Compiling the production TU as its own object target keeps that distinction.
add_library(gl_log_host_test_object OBJECT
    "${GL_COMMON_ROOT}/common/gl_log.c"
)
target_include_directories(gl_log_host_test_object PRIVATE "${GL_COMMON_ROOT}")
target_compile_definitions(gl_log_host_test_object PRIVATE
    write=gl_log_test_write
)
target_compile_options(gl_log_host_test_object PRIVATE ${GL_HOST_WARNINGS})

add_executable(gl_log_test
    "${GL_REPO_ROOT}/tools/tests/gl_log_test.c"
    $<TARGET_OBJECTS:gl_log_host_test_object>
)
target_include_directories(gl_log_test PRIVATE "${GL_COMMON_ROOT}")
target_compile_options(gl_log_test PRIVATE ${GL_HOST_WARNINGS})
add_test(NAME gl_log_test COMMAND gl_log_test)
list(APPEND GL_REGISTERED_CONTRACT_SOURCES tools/tests/gl_log_test.c)

# Fail closed when the machine disposition and this module's CTest registry
# diverge. The root inventory separately proves that every repository candidate
# has exactly one disposition row.
if(NOT EXISTS "${GL_DISPOSITION}")
    message(FATAL_ERROR "native test disposition is missing: ${GL_DISPOSITION}")
endif()
file(STRINGS "${GL_DISPOSITION}" GL_DISPOSITION_LINES)
set(GL_EXPECTED_CONTRACT_SOURCES "")
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
    if(status STREQUAL "host-ctest" AND
       module STREQUAL "native-contracts")
        list(APPEND GL_EXPECTED_CONTRACT_SOURCES "${source}")
    endif()
endforeach()
list(SORT GL_EXPECTED_CONTRACT_SOURCES)
list(SORT GL_REGISTERED_CONTRACT_SOURCES)
if(NOT "${GL_EXPECTED_CONTRACT_SOURCES}" STREQUAL
       "${GL_REGISTERED_CONTRACT_SOURCES}")
    message(FATAL_ERROR
        "native-contracts registry drift: disposition=${GL_EXPECTED_CONTRACT_SOURCES}; "
        "cmake=${GL_REGISTERED_CONTRACT_SOURCES}")
endif()
