include_guard(GLOBAL)

# Purpose: build DFRoot's 43284 executable adapter (target `glued43284_exec`,
# OUTPUT_NAME `43284Glued`) and its bootstrap helper (`glued43284_bootstrap`,
# OUTPUT_NAME `bootstrap`). Both stay executable ELFs packaged as lib*.so; making them
# SHARED would change the attack-payload execution semantics.
# Inputs: pinned upstream C/ASM, logger adapter sources, embedded KO files, and
# the arm64 splicehelper import; pristine/adapter source hashes live in SOURCE-MANIFEST.json.
# Output: lib43284Glued.so plus libbootstrap.so in AGP's native output directory.
# Failure: the NDK compile/link or helper generation fails the variant build.
# Preconditions: included by backends/CMakeLists.txt when GHOSTLOCK_BACKEND_43284_GLUED=ON;
# the Android C/ASM toolchains, AGP output dir, embedded KO files and helper exist.
# Phase: application native build; payload executables are not run by CMake.
# Note: included via include(), so every path is anchored at ${CMAKE_CURRENT_LIST_DIR}.

#TODO(m0-marker-20261029): isolate the approved marker-only device fixture; remove this
#branch and the matching Gradle property after the M0 device gate, keeping the default
#upstream CMake path below byte-identical.
if(GHOSTLOCK_M0_MARKER)
    if(NOT DEFINED GHOSTLOCK_M0_SOURCE_DIR OR NOT IS_DIRECTORY "${GHOSTLOCK_M0_SOURCE_DIR}")
        message(FATAL_ERROR "M0 fixture source directory was not staged by Gradle")
    endif()
    set(M0_SOURCE_DIR "${GHOSTLOCK_M0_SOURCE_DIR}")
    set(M0_KO_FILES
        "${M0_SOURCE_DIR}/ko/dfroot-android12-5.10.ko"
        "${M0_SOURCE_DIR}/ko/dfroot-android13-5.10.ko"
        "${M0_SOURCE_DIR}/ko/dfroot-android13-5.15.ko"
        "${M0_SOURCE_DIR}/ko/dfroot-android14-5.15.ko"
        "${M0_SOURCE_DIR}/ko/dfroot-android14-6.1.ko"
        "${M0_SOURCE_DIR}/ko/dfroot-android15-6.6.ko"
        "${M0_SOURCE_DIR}/ko/dfroot-android16-6.12.ko"
        "${M0_SOURCE_DIR}/ko/dfroot-android17-6.18.ko"
    )
    add_executable(glued43284_exec
        "${M0_SOURCE_DIR}/exp.c"
        "${M0_SOURCE_DIR}/m0_marker.c"
        "${M0_SOURCE_DIR}/libcxx.S"
        "${M0_SOURCE_DIR}/elf_parser.c"
    )
    target_include_directories(glued43284_exec PRIVATE "${M0_SOURCE_DIR}")
    target_compile_options(glued43284_exec PRIVATE
        -Wall -Wextra -fno-stack-protector -fomit-frame-pointer
        "-Wa,-I${M0_SOURCE_DIR}"
    )
    target_link_libraries(glued43284_exec PRIVATE log)
    target_link_options(glued43284_exec PRIVATE
        -fPIE -pie -pthread -Wl,-z,max-page-size=16384
    )
    set_target_properties(glued43284_exec PROPERTIES
        PREFIX "lib"
        SUFFIX ".so"
        OUTPUT_NAME "43284Glued"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
    )
    set_source_files_properties("${M0_SOURCE_DIR}/exp.c" PROPERTIES
        OBJECT_DEPENDS "${M0_KO_FILES};${M0_SOURCE_DIR}/splicehelper"
    )
    return()
endif()

set(DFROOT_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}")
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
    ${DFROOT_SOURCE_DIR}/exp.c
    ${DFROOT_SOURCE_DIR}/libcxx.S
    ${DFROOT_SOURCE_DIR}/elf_parser.c
)
target_include_directories(glued43284_exec PRIVATE "${DFROOT_SOURCE_DIR}")
target_compile_options(glued43284_exec PRIVATE
    -Wall
    -Wextra
    -fno-stack-protector
    -fomit-frame-pointer
    "-Wa,-I${DFROOT_SOURCE_DIR}"
)
target_link_libraries(glued43284_exec PRIVATE gl_common log)
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

add_executable(glued43284_bootstrap ${DFROOT_SOURCE_DIR}/bootstrap.c)
target_compile_options(glued43284_bootstrap PRIVATE -Wall -Wextra)
target_link_options(glued43284_bootstrap PRIVATE
    -fPIE
    -pie
    -Wl,-z,max-page-size=16384
)
target_link_libraries(glued43284_bootstrap PRIVATE gl_common log)
set_target_properties(glued43284_bootstrap PROPERTIES
    PREFIX "lib"
    SUFFIX ".so"
    OUTPUT_NAME "bootstrap"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_LIBRARY_OUTPUT_DIRECTORY}"
)
