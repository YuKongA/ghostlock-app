cmake_minimum_required(VERSION 3.22)
project(exp)
enable_language(ASM)

set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} -I${CMAKE_SOURCE_DIR}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${C_FLAGS} -I${CMAKE_SOURCE_DIR} -Wall -Wextra -fno-stack-protector -fomit-frame-pointer")

set(SPLICEHELPER "${CMAKE_CURRENT_SOURCE_DIR}/splicehelper")
add_custom_command(
    OUTPUT "${SPLICEHELPER}"
    COMMAND "${CMAKE_C_COMPILER}"
            --target=${CMAKE_C_COMPILER_TARGET}
            --sysroot=${CMAKE_SYSROOT}
            "${CMAKE_CURRENT_SOURCE_DIR}/splicehelper.c"
            -o "${SPLICEHELPER}"
            -nodefaultlibs -nostartfiles -ffreestanding -static
    COMMAND "${CMAKE_STRIP}" "${SPLICEHELPER}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/splicehelper.c"
    COMMENT "Building splicehelper"
)
add_custom_target(splicehelper_bin DEPENDS "${SPLICEHELPER}")

add_library(exp SHARED exp.c libcxx.S elf_parser.c)
add_dependencies(exp splicehelper_bin)

set_source_files_properties(exp.c PROPERTIES OBJECT_DEPENDS
    "${CMAKE_SOURCE_DIR}/ko/dirtyfrag-android12-5.10.ko;\
${CMAKE_SOURCE_DIR}/ko/dirtyfrag-android13-5.10.ko;\
${CMAKE_SOURCE_DIR}/ko/dirtyfrag-android13-5.15.ko;\
${CMAKE_SOURCE_DIR}/ko/dirtyfrag-android14-5.15.ko;\
${CMAKE_SOURCE_DIR}/ko/dirtyfrag-android14-6.1.ko;\
${CMAKE_SOURCE_DIR}/ko/dirtyfrag-android15-6.6.ko;\
${CMAKE_SOURCE_DIR}/ko/dirtyfrag-android16-6.12.ko;\
${CMAKE_SOURCE_DIR}/ko/dirtyfrag-android17-6.18.ko;\
${CMAKE_SOURCE_DIR}/splicehelper")

target_link_libraries(exp log)
target_link_options(exp PRIVATE -Wl,-z,max-page-size=16384)
