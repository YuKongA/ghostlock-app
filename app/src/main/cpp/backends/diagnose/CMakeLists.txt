include_guard(GLOBAL)

# Purpose: build the sole non-attack native Diagnose operation as one ordinary-C shared library.
# Inputs: diagnose.cpp, its direct same-release API header, and the existing gl_common logger object target.
# Output: libghostlock_diagnose.so with exactly gl_diagnose_run as its public project symbol.
# Failure: warnings, unexpected exports, or linker alignment failures stop the variant build.
# Preconditions: included after common/CMakeLists.txt has declared gl_common.
# Phase: P-DIAG-NATIVE-001/批次01.

set(GL_DIAGNOSE_VERSION_SCRIPT
    "${CMAKE_CURRENT_BINARY_DIR}/ghostlock_diagnose.exports.map")
file(GENERATE
    OUTPUT "${GL_DIAGNOSE_VERSION_SCRIPT}"
    CONTENT "{\n  global:\n    gl_diagnose_run;\n  local:\n    *;\n};\n")

add_library(ghostlock_diagnose SHARED
    ${CMAKE_CURRENT_LIST_DIR}/diagnose.cpp
)
target_include_directories(ghostlock_diagnose PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../..
)
target_compile_features(ghostlock_diagnose PRIVATE cxx_std_23)
target_compile_options(ghostlock_diagnose PRIVATE
    -Wall
    -Wextra
    -Wconversion
    -Wsign-conversion
    -Werror
)
target_link_libraries(ghostlock_diagnose PRIVATE
    gl_common
    log
)
target_link_options(ghostlock_diagnose PRIVATE
    "-Wl,--version-script=${GL_DIAGNOSE_VERSION_SCRIPT}"
    "-Wl,-z,max-page-size=16384"
)
set_target_properties(ghostlock_diagnose PROPERTIES
    OUTPUT_NAME ghostlock_diagnose
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    GHOSTLOCK_EXPECTED_EXPORTS gl_diagnose_run
)
