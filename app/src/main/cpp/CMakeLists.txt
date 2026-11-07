cmake_minimum_required(VERSION 3.22)

# Purpose: Android app-owned native build entry point. Only project/standard setup plus
# two assembly includes; common owns the shared logger object target and
# backends/CMakeLists.txt admits only the non-attack Diagnose DSO.
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
