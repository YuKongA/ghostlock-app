cmake_minimum_required(VERSION 3.20)
project(ghostlock_skeleton LANGUAGES C CXX)

# A0 skeleton build entry (rewrite-plan-v2 section 2). Mirrors src/Makefile and
# references ONLY the new tree (core/**) plus the vendored mpack. The Android
# cross build is a later phase and lives on the Gradle side; this file is the
# host build for editors and local checks.

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SKELETON_WARNINGS -Wall -Wextra -Wconversion -Wsign-conversion)

add_executable(ghostlock-skeleton
  core/main.cpp
  core/contract/layer.hpp
  core/contract/layer_scope.hpp
  core/contract/kind.hpp
  core/contract/resource_scope.hpp
  core/contract/op_registry.hpp
  core/support/layer.hpp
  core/memory/layer.hpp
  core/race/layer.hpp
  core/backend/layer.hpp
  core/pipeline/layer.hpp
  core/pipeline/attack_window.hpp
  core/pipeline/composition_root.hpp
  core/pipeline/plan_gate.hpp
  core/terminal/layer.hpp
  core/script/layer.hpp
  core/session/layer.hpp
  core/profile/layer.hpp
  core/profile/route_catalog.hpp
)
target_include_directories(ghostlock-skeleton PRIVATE core)
target_compile_options(ghostlock-skeleton PRIVATE ${SKELETON_WARNINGS})

# Vendored MessagePack: C99 amalgamation, its own flags, warnings off, so the
# project's zero-warning line above stays meaningful (lib/mpack/README.md).
add_library(mpack STATIC lib/mpack/mpack.c)
target_include_directories(mpack PUBLIC lib/mpack)
set_source_files_properties(lib/mpack/mpack.c PROPERTIES COMPILE_OPTIONS "-w")
target_link_libraries(ghostlock-skeleton PRIVATE mpack)

# P4 evidence binaries (restored in A0, see src/Makefile check target).
add_executable(window-exit-paths core/pipeline/tests/window_exit_paths_test.cpp)
add_executable(plan-gate core/pipeline/tests/plan_gate_test.cpp)
foreach(evidence_target window-exit-paths plan-gate)
  target_include_directories(${evidence_target} PRIVATE core)
  target_compile_options(${evidence_target} PRIVATE ${SKELETON_WARNINGS})
endforeach()
