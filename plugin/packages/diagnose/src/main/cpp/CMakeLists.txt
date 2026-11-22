cmake_minimum_required(VERSION 3.22)
project(ghostlock_diagnose_plugin LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(GL_COMMON_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../../native-source/common")
include(${GL_COMMON_ROOT}/CMakeLists.txt)

set(GL_DIAGNOSE_VERSION_SCRIPT
    "${CMAKE_CURRENT_BINARY_DIR}/ghostlock_diagnose.exports.map")
file(GENERATE
    OUTPUT "${GL_DIAGNOSE_VERSION_SCRIPT}"
    CONTENT "{\n  global:\n    gl_diagnose_run;\n  local:\n    *;\n};\n")

add_library(ghostlock_diagnose SHARED diagnose.cpp)
target_include_directories(ghostlock_diagnose PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${GL_COMMON_ROOT}
)
target_compile_features(ghostlock_diagnose PRIVATE cxx_std_23)
target_compile_options(ghostlock_diagnose PRIVATE
    -Wall
    -Wextra
    -Wconversion
    -Wsign-conversion
    -Werror
)
target_link_libraries(ghostlock_diagnose PRIVATE gl_common log)
target_link_options(ghostlock_diagnose PRIVATE
    "-Wl,--version-script=${GL_DIAGNOSE_VERSION_SCRIPT}"
    "-Wl,-z,max-page-size=16384"
    "-Wl,-soname,libghostlock_diagnose.so"
)
set_target_properties(ghostlock_diagnose PROPERTIES
    OUTPUT_NAME ghostlock_diagnose
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    GHOSTLOCK_EXPECTED_EXPORTS gl_diagnose_run
)
