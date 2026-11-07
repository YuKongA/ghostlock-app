include_guard(GLOBAL)

# Purpose: include the non-attack Diagnose DSO while keeping every real-attack backend disconnected.
# Inputs: the fixed Diagnose CMake chunk; backend enable flags remain intentionally absent.
# Output: libghostlock_diagnose.so only; no attack executable or attack DSO enters the APK.
# Failure: any additional backend include or target violates the disconnected state.
# Preconditions: included by the top-level CMakeLists after common/.
# Phase: application native build.
# Note: 43499-Glued, 43284-Glued, and 98260 remain source-only historical candidates.
# Reconnection requires a new approved backend design; no Gradle property can bypass
# this boundary.

include(${CMAKE_CURRENT_LIST_DIR}/diagnose/CMakeLists.txt)
