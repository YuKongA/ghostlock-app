include_guard(GLOBAL)

# Purpose: original CVE-2026-43284 backend chunk (DFRoot-derived in-process payload).
# Defines the `ghostlock43284` shared library; target/output name is unchanged from the
# pre-chunk top-level build file.
# Inputs: pinned upstream C/ASM sources, logger adapter sources, embedded KO payloads
# and the checked-in arm64 splicehelper import that exp.c depends on.
# Output: libghostlock43284.so in AGP's native output directory when
# GHOSTLOCK_BACKEND_43284=ON.
# Failure: the NDK compile/link fails the variant build.
# Preconditions: included by backends/CMakeLists.txt only when GHOSTLOCK_BACKEND_43284=ON;
# gl_common already exists and the embedded KO/splicehelper files are present.
# Phase: application native build; the payload is executed on devices at runtime.
# Note: included via include(), so every path is anchored at ${CMAKE_CURRENT_LIST_DIR}.

# AGP packages an executable named lib*.so into nativeLibraryDir; the app execs it.
set(GLK_43284_KO
    ${CMAKE_CURRENT_LIST_DIR}/ko/dfroot-android12-5.10.ko
    ${CMAKE_CURRENT_LIST_DIR}/ko/dfroot-android13-5.10.ko
    ${CMAKE_CURRENT_LIST_DIR}/ko/dfroot-android13-5.15.ko
    ${CMAKE_CURRENT_LIST_DIR}/ko/dfroot-android14-5.15.ko
    ${CMAKE_CURRENT_LIST_DIR}/ko/dfroot-android14-6.1.ko
    ${CMAKE_CURRENT_LIST_DIR}/ko/dfroot-android15-6.6.ko
    ${CMAKE_CURRENT_LIST_DIR}/ko/dfroot-android16-6.12.ko
    ${CMAKE_CURRENT_LIST_DIR}/ko/dfroot-android17-6.18.ko
)
add_library(ghostlock43284 SHARED
    ${CMAKE_CURRENT_LIST_DIR}/exp.c
    ${CMAKE_CURRENT_LIST_DIR}/libcxx.S
    ${CMAKE_CURRENT_LIST_DIR}/elf_parser.c
    ${CMAKE_CURRENT_LIST_DIR}/df_log.c
    ${CMAKE_CURRENT_LIST_DIR}/glk43284_api.c
    ${CMAKE_CURRENT_LIST_DIR}/jni_attach.c
    ${CMAKE_CURRENT_LIST_DIR}/jni_exploit.c
)
target_include_directories(ghostlock43284 PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/../../common
)
target_link_libraries(ghostlock43284 PRIVATE gl_common log)
target_link_options(ghostlock43284 PRIVATE -Wl,-z,max-page-size=16384)
set_target_properties(ghostlock43284 PROPERTIES
    PREFIX "lib"
    SUFFIX ".so"
    OUTPUT_NAME ghostlock43284
    C_VISIBILITY_PRESET hidden
)
set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/exp.c PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/splicehelper;${GLK_43284_KO}"
)
