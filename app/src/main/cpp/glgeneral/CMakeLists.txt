include_guard(GLOBAL)

# Purpose: Kotlin-only DSO (KernelSnitch discovery today). The scan worker threads,
# mmap'd shared state and 64 GiB futex arena only make sense in one dedicated DSO;
# backend chunks must not carry a second copy.
# Inputs: general_jni.c plus the kernelsnitch headers in this directory.
# Output: shared library target `ghostlock_general` (name unchanged, lib*.so).
# Failure: compile/link errors fail the variant build.
# Preconditions: included by the top-level CMakeLists after common/ (needs gl_common).
# Phase: application native build.

add_library(ghostlock_general SHARED
    ${CMAKE_CURRENT_LIST_DIR}/general_jni.c
)
target_include_directories(ghostlock_general PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
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
