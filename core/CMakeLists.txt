cmake_minimum_required(VERSION 3.20)
project(ghostlock_skeleton LANGUAGES C CXX)

# CLion-only index model. The production build remains Makefile/Gradle driven;
# this file supplies source membership and include paths for code completion.

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SKELETON_WARNINGS -Wall -Wextra -Wconversion -Wsign-conversion)

# Keep the source root visible even when CLion indexes a header as a standalone
# file instead of through a compiled translation unit.
include_directories(
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}/lib/lua/src"
)

# TODO(dynamic-src-sources): keep this list derived from src/ so new production
# files are visible to CLion without changing the production build system.
file(GLOB_RECURSE GHOSTLOCK_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*"
)
list(FILTER GHOSTLOCK_SOURCES EXCLUDE REGEX "/tests?(/|$)")

# CLion may resolve an opened header independently from its including file.
# Publish every source subdirectory so both qualified and local header names
# remain navigable in that mode.
set(GHOSTLOCK_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src")
file(GLOB_RECURSE GHOSTLOCK_TREE CONFIGURE_DEPENDS
  LIST_DIRECTORIES true
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*"
)
foreach(path IN LISTS GHOSTLOCK_TREE)
  if(IS_DIRECTORY "${path}" AND NOT path MATCHES "/tests?(/|$)")
    list(APPEND GHOSTLOCK_INCLUDE_DIRS "${path}")
  endif()
endforeach()
list(REMOVE_DUPLICATES GHOSTLOCK_INCLUDE_DIRS)
include_directories(${GHOSTLOCK_INCLUDE_DIRS})

add_executable(ghostlock-skeleton ${GHOSTLOCK_SOURCES})
target_include_directories(ghostlock-skeleton PRIVATE
  ${GHOSTLOCK_INCLUDE_DIRS}
  "${CMAKE_CURRENT_SOURCE_DIR}/lib/lua/src"
)
target_compile_options(ghostlock-skeleton PRIVATE ${SKELETON_WARNINGS})

# Vendored MessagePack: C99 amalgamation, its own flags, warnings off, so the
# project's zero-warning line above stays meaningful (lib/mpack/README.md).
add_library(mpack STATIC lib/mpack/mpack.c)
target_include_directories(mpack PUBLIC lib/mpack)
set_source_files_properties(lib/mpack/mpack.c PROPERTIES COMPILE_OPTIONS "-w")
target_link_libraries(ghostlock-skeleton PRIVATE mpack)

# P4 evidence binaries (restored in A0, see src/Makefile check target).
add_executable(window-exit-paths src/pipeline/tests/window_exit_paths_test.cpp)
add_executable(plan-gate src/pipeline/tests/plan_gate_test.cpp)
foreach(evidence_target window-exit-paths plan-gate)
  target_include_directories(${evidence_target} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
  )
  target_compile_options(${evidence_target} PRIVATE ${SKELETON_WARNINGS})
endforeach()
