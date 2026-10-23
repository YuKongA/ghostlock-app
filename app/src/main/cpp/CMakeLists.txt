cmake_minimum_required(VERSION 3.22)

# Android app-owned native build. All native sources live under this directory.
project(ghostlock_native LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(gl_general STATIC
    general/gl_log.c
)
target_include_directories(gl_general PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(gl_general PUBLIC log)

# 43284: DFRoot-derived standalone payload.
# AGP packages an executable named lib*.so into nativeLibraryDir; the app execs it.
set(GLK_43284_KO
    ${CMAKE_CURRENT_SOURCE_DIR}/glk43284/ko/dfroot-android12-5.10.ko
    ${CMAKE_CURRENT_SOURCE_DIR}/glk43284/ko/dfroot-android13-5.10.ko
    ${CMAKE_CURRENT_SOURCE_DIR}/glk43284/ko/dfroot-android13-5.15.ko
    ${CMAKE_CURRENT_SOURCE_DIR}/glk43284/ko/dfroot-android14-5.15.ko
    ${CMAKE_CURRENT_SOURCE_DIR}/glk43284/ko/dfroot-android14-6.1.ko
    ${CMAKE_CURRENT_SOURCE_DIR}/glk43284/ko/dfroot-android15-6.6.ko
    ${CMAKE_CURRENT_SOURCE_DIR}/glk43284/ko/dfroot-android16-6.12.ko
    ${CMAKE_CURRENT_SOURCE_DIR}/glk43284/ko/dfroot-android17-6.18.ko
)
add_library(ghostlock43284 SHARED
    glk43284/exp.c
    glk43284/libcxx.S
    glk43284/elf_parser.c
    glk43284/df_log.c
    glk_log/glk_log.c
    glk43284/glk43284_api.c
    glk43284/jni_attach.c
    glk43284/jni_exploit.c
)
target_include_directories(ghostlock43284 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/glk43284
    ${CMAKE_CURRENT_SOURCE_DIR}/general
)
target_link_libraries(ghostlock43284 PRIVATE gl_general log)
target_link_options(ghostlock43284 PRIVATE -Wl,-z,max-page-size=16384)
set_target_properties(ghostlock43284 PROPERTIES
    PREFIX "lib"
    SUFFIX ".so"
    OUTPUT_NAME ghostlock43284
    C_VISIBILITY_PRESET hidden
)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/glk43284/exp.c PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/glk43284/splicehelper;${GLK_43284_KO}"
)

# 43499: backend-specific payload.
add_library(ghostlock43499 SHARED
    glk43499/main.c
    glk43499/fops.c
    glk43499/util.c
    glk43499/glk43499_fixed_515.c
    glk43499/glk_session.c
    glk_log/glk_log.c
    glk43499/glk43499_api.c
    glk43499/jni_attach.c
    glk43499/jni_exploit.c
)
target_include_directories(ghostlock43499 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/glk43499
    ${CMAKE_CURRENT_SOURCE_DIR}/glk43499/kernels
    ${CMAKE_CURRENT_SOURCE_DIR}/glkcommon
)
target_compile_definitions(ghostlock43499 PRIVATE TARGET_CONFIG_H="target.h" GLK_NO_KNOWN_OFFSETS=1)
target_link_libraries(ghostlock43499 PRIVATE gl_general log)
target_link_options(ghostlock43499 PRIVATE -Wl,-z,max-page-size=16384)
set_target_properties(ghostlock43499 PROPERTIES
    PREFIX "lib"
    SUFFIX ".so"
    OUTPUT_NAME ghostlock43499
    C_VISIBILITY_PRESET hidden
)

# Legacy JNI shell: keeps NativeLogBridge's first library loadable while the
# real per-DSO log sink is designed in P6c.
add_library(ghostlock_jni SHARED
    glk_jni/legacy_attach.c
)
target_link_libraries(ghostlock_jni PRIVATE log)
target_link_options(ghostlock_jni PRIVATE -Wl,-z,max-page-size=16384)
set_target_properties(ghostlock_jni PROPERTIES
    PREFIX "lib"
    SUFFIX ".so"
    OUTPUT_NAME ghostlock_jni
)
