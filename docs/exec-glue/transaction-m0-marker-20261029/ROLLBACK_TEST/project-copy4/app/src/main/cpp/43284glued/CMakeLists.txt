# Purpose: build DFRoot's unchanged 43284 executable and its bootstrap helper.
# Inputs: upstream C/ASM, embedded KO files, and the arm64 splicehelper import
# asset recorded in src/upstream/SOURCE-MANIFEST.json.
# Output: lib43284Glued.so plus libbootstrap.so in AGP's native output directory.
# Failure: the NDK compile/link or helper generation fails the variant build.
# Preconditions: the parent project supplies Android C/ASM toolchains and AGP
# output directories; the checked-in helper hash matches the import manifest.
# Phase: application native build; payload executables are not run by CMake.

set(DFROOT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(DFROOT_SPLICEHELPER "${DFROOT_SOURCE_DIR}/splicehelper")

set(DFROOT_KO_FILES
    "${DFROOT_SOURCE_DIR}/ko/dfroot-android12-5.10.ko"
    "${DFROOT_SOURCE_DIR}/ko/dfroot-android13-5.10.ko"
    "${DFROOT_SOURCE_DIR}/ko/dfroot-android13-5.15.ko"
    "${DFROOT_SOURCE_DIR}/ko/dfroot-android14-5.15.ko"
    "${DFROOT_SOURCE_DIR}/ko/dfroot-android14-6.1.ko"
    "${DFROOT_SOURCE_DIR}/ko/dfroot-android15-6.6.ko"
    "${DFROOT_SOURCE_DIR}/ko/dfroot-android16-6.12.ko"
    "${DFROOT_SOURCE_DIR}/ko/dfroot-android17-6.18.ko"
)

add_executable(glued43284_exec
    exp.c
    libcxx.S
    elf_parser.c
)
target_include_directories(glued43284_exec PRIVATE "${DFROOT_SOURCE_DIR}")
target_compile_options(glued43284_exec PRIVATE
    -Wall
    -Wextra
    -fno-stack-protector
    -fomit-frame-pointer
    "-Wa,-I${DFROOT_SOURCE_DIR}"
)
target_link_libraries(glued43284_exec PRIVATE log)
target_link_options(glued43284_exec PRIVATE
    -fPIE
    -pie
    -pthread
    -Wl,-z,max-page-size=16384
)
set_target_properties(glued43284_exec PROPERTIES
    PREFIX "lib"
    SUFFIX ".so"
    OUTPUT_NAME "43284Glued"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
)
set_source_files_properties("${DFROOT_SOURCE_DIR}/exp.c" PROPERTIES
    OBJECT_DEPENDS "${DFROOT_KO_FILES};${DFROOT_SPLICEHELPER}"
)

add_executable(glued43284_bootstrap bootstrap.c)
target_compile_options(glued43284_bootstrap PRIVATE -Wall -Wextra)
target_link_options(glued43284_bootstrap PRIVATE
    -fPIE
    -pie
    -Wl,-z,max-page-size=16384
)
set_target_properties(glued43284_bootstrap PROPERTIES
    PREFIX "lib"
    SUFFIX ".so"
    OUTPUT_NAME "bootstrap"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
)
