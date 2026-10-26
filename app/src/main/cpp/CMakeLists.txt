cmake_minimum_required(VERSION 3.22)

# Android app-owned native build. All native sources live under this directory.
project(ghostlock_native LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(gl_common STATIC
        common/gl_log.c
)
target_include_directories(gl_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(gl_common PUBLIC log)

# general: Kotlin-only DSO (KernelSnitch discovery today).
# The scan worker threads, mmap'd shared state and 64 GiB futex arena only make
# sense in one dedicated DSO; the backend libraries must not carry a second copy.
add_library(ghostlock_general SHARED
    glgeneral/general_jni.c
)
target_include_directories(ghostlock_general PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/glgeneral
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

# 43284: DFRoot-derived standalone payload.
# AGP packages an executable named lib*.so into nativeLibraryDir; the app execs it.
set(GLK_43284_KO
    ${CMAKE_CURRENT_SOURCE_DIR}/gl43284/ko/dfroot-android12-5.10.ko
    ${CMAKE_CURRENT_SOURCE_DIR}/gl43284/ko/dfroot-android13-5.10.ko
    ${CMAKE_CURRENT_SOURCE_DIR}/gl43284/ko/dfroot-android13-5.15.ko
    ${CMAKE_CURRENT_SOURCE_DIR}/gl43284/ko/dfroot-android14-5.15.ko
    ${CMAKE_CURRENT_SOURCE_DIR}/gl43284/ko/dfroot-android14-6.1.ko
    ${CMAKE_CURRENT_SOURCE_DIR}/gl43284/ko/dfroot-android15-6.6.ko
    ${CMAKE_CURRENT_SOURCE_DIR}/gl43284/ko/dfroot-android16-6.12.ko
    ${CMAKE_CURRENT_SOURCE_DIR}/gl43284/ko/dfroot-android17-6.18.ko
)
add_library(ghostlock43284 SHARED
    gl43284/exp.c
    gl43284/libcxx.S
    gl43284/elf_parser.c
    gl43284/df_log.c
    gl43284/glk43284_api.c
    gl43284/jni_attach.c
    gl43284/jni_exploit.c
)
target_include_directories(ghostlock43284 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/gl43284
        ${CMAKE_CURRENT_SOURCE_DIR}/common
)
target_link_libraries(ghostlock43284 PRIVATE gl_common log)
target_link_options(ghostlock43284 PRIVATE -Wl,-z,max-page-size=16384)
set_target_properties(ghostlock43284 PROPERTIES
    PREFIX "lib"
    SUFFIX ".so"
    OUTPUT_NAME ghostlock43284
    C_VISIBILITY_PRESET hidden
)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/gl43284/exp.c PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/gl43284/splicehelper;${GLK_43284_KO}"
)

# 43499: backend-specific payload.
# The legacy C tree is parked under gl43499/{legacy,very_legacy}; this batch
# compiles the new JNI transport plus an explicit fail-closed unavailable core.
add_library(ghostlock43499 SHARED
    gl43499/jni43499.c
    gl43499/core/api_unavailable.c
)
target_include_directories(ghostlock43499 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/gl43499
    ${CMAKE_CURRENT_SOURCE_DIR}/glgeneral
)
target_link_libraries(ghostlock43499 PRIVATE gl_common log)
target_link_options(ghostlock43499 PRIVATE -Wl,-z,max-page-size=16384)
set_target_properties(ghostlock43499 PROPERTIES
    PREFIX "lib"
    SUFFIX ".so"
    OUTPUT_NAME ghostlock43499
    C_VISIBILITY_PRESET hidden
)

