include_guard(GLOBAL)

# Purpose: backend assembly chunk. Declares exactly one compile switch per backend and
# includes the matching backend chunk CMakeLists.txt only when that switch is ON; OFF
# keeps the chunk out of the CMake target graph (and therefore out of the APK) without
# touching the backend sources.
# Inputs: -DGHOSTLOCK_BACKEND_* cache options from AGP/Gradle (-P... properties) or CLI.
# Output: no target here; target names/output names are owned by the included chunks and
# unchanged from the pre-chunk top-level build file.
# Failure: a chunk included with ON fails configuration loudly when it cannot build
# (e.g. GHOSTLOCK_BACKEND_98260=ON while its sources are missing).
# Preconditions: included by the top-level CMakeLists after common/ and glgeneral/.
# Phase: application native build.
# Note: switched-on chunks are `include()`d (not add_subdirectory'd), so each chunk
# anchors paths at its own ${CMAKE_CURRENT_LIST_DIR}.

option(GHOSTLOCK_BACKEND_43499_GLUED "Build the 43499-Glued standalone payload" ON)
option(GHOSTLOCK_BACKEND_43284_GLUED "Build the 43284Glued payload and bootstrap" ON)
# Legacy CLI compatibility: this switch only supplies the default of the two original
# backends below; the chunks themselves are gated by the per-backend switches.
option(GHOSTLOCK_ENABLE_ORIGINAL_BACKENDS "Legacy alias: default for original backends" OFF)
option(GHOSTLOCK_BACKEND_43499 "Build the original CVE-2026-43499 backend" ${GHOSTLOCK_ENABLE_ORIGINAL_BACKENDS})
option(GHOSTLOCK_BACKEND_43284 "Build the original CVE-2026-43284 backend" ${GHOSTLOCK_ENABLE_ORIGINAL_BACKENDS})
option(GHOSTLOCK_BACKEND_98260 "Build the A301SO 98260 standalone experiment helper" OFF)

if(GHOSTLOCK_BACKEND_43499_GLUED)
    include(${CMAKE_CURRENT_LIST_DIR}/43499glued/CMakeLists.txt)
endif()
if(GHOSTLOCK_BACKEND_43284_GLUED)
    include(${CMAKE_CURRENT_LIST_DIR}/43284glued/CMakeLists.txt)
endif()
if(GHOSTLOCK_BACKEND_43499)
    include(${CMAKE_CURRENT_LIST_DIR}/43499/CMakeLists.txt)
endif()
if(GHOSTLOCK_BACKEND_43284)
    include(${CMAKE_CURRENT_LIST_DIR}/43284/CMakeLists.txt)
endif()
if(GHOSTLOCK_BACKEND_98260)
    include(${CMAKE_CURRENT_LIST_DIR}/98260/CMakeLists.txt)
endif()
