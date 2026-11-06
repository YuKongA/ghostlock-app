cmake_minimum_required(VERSION 3.22)

# Purpose: Android app-owned native build entry point. Only project/standard setup plus
# two assembly includes; common owns the only current native object target and
# backends/CMakeLists.txt is the explicit real-attack disconnection boundary.
# Inputs: the Android arm64 NDK toolchain and AGP output directories come from AGP.
# Output: no target here; targets are defined by the included files.
# Failure: any reintroduced backend target violates the disconnected graph contract.
# Preconditions: invoked by AGP externalNativeBuild (CMake >= 3.22).
# Phase: application native build.
# Note: `include()` keeps CMAKE_CURRENT_SOURCE_DIR/CMAKE_CURRENT_BINARY_DIR pointing at
# this directory, so every chunk anchors its paths at ${CMAKE_CURRENT_LIST_DIR} instead.

project(ghostlock_native LANGUAGES C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(${CMAKE_CURRENT_LIST_DIR}/common/CMakeLists.txt)
include(${CMAKE_CURRENT_LIST_DIR}/backends/CMakeLists.txt)

# Purpose: export the enabled app-native target graph for the post-merge AGP
# verifier without duplicating artifact or JNI symbol lists in Gradle.
# Inputs: targets created by the two includes above and their actual source
# files; GHOSTLOCK_NATIVE_ARTIFACT_MANIFEST is supplied only for debug by AGP.
# Output: one stable four-column TSV consumed by verifyDebugNativeArtifacts.
# Failure: unreadable target metadata fails configuration; an omitted manifest
# argument leaves direct/non-AGP configure unchanged.
# Preconditions: run after every app-owned target has been declared.
# Phase: D7.3+D7.4-N Android native artifact verification.
if(DEFINED GHOSTLOCK_NATIVE_ARTIFACT_MANIFEST AND
   NOT GHOSTLOCK_NATIVE_ARTIFACT_MANIFEST STREQUAL "")
    get_property(GL_APP_TARGETS DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
    set(GL_MANIFEST_TARGETS "")
    foreach(target IN LISTS GL_APP_TARGETS)
        get_target_property(target_type ${target} TYPE)
        if(target_type STREQUAL "EXECUTABLE" OR
           target_type STREQUAL "SHARED_LIBRARY")
            list(APPEND GL_MANIFEST_TARGETS ${target})
        endif()
    endforeach()
    list(REMOVE_DUPLICATES GL_MANIFEST_TARGETS)
    list(SORT GL_MANIFEST_TARGETS)

    set(GL_NATIVE_MANIFEST
        "target\tartifact\ttype\texpected_jni\n")
    foreach(target IN LISTS GL_MANIFEST_TARGETS)
        get_target_property(target_type ${target} TYPE)
        get_target_property(target_sources ${target} SOURCES)
        set(target_jni "")
        foreach(source IN LISTS target_sources)
            if(IS_ABSOLUTE "${source}")
                set(source_path "${source}")
            else()
                get_filename_component(source_path "${source}" ABSOLUTE
                    BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
            endif()
            if(EXISTS "${source_path}" AND NOT IS_DIRECTORY "${source_path}")
                file(READ "${source_path}" source_text)
                string(REGEX MATCHALL "Java_[A-Za-z0-9_]+"
                    source_jni "${source_text}")
                list(APPEND target_jni ${source_jni})
            endif()
        endforeach()
        list(REMOVE_DUPLICATES target_jni)
        list(SORT target_jni)
        string(JOIN "," target_jni_csv ${target_jni})
        string(APPEND GL_NATIVE_MANIFEST
            "${target}\t$<TARGET_FILE_NAME:${target}>\t${target_type}\t${target_jni_csv}\n")
    endforeach()

    get_filename_component(GL_NATIVE_MANIFEST_DIR
        "${GHOSTLOCK_NATIVE_ARTIFACT_MANIFEST}" DIRECTORY)
    file(MAKE_DIRECTORY "${GL_NATIVE_MANIFEST_DIR}")
    file(GENERATE
        OUTPUT "${GHOSTLOCK_NATIVE_ARTIFACT_MANIFEST}"
        CONTENT "${GL_NATIVE_MANIFEST}")
endif()
