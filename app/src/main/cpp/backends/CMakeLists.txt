include_guard(GLOBAL)

# Purpose: explicit disconnection boundary for every retained real-attack backend.
# Inputs: none; backend enable flags are intentionally absent from the app build.
# Output: no backend target and therefore no attack executable or DSO in the APK.
# Failure: adding an include or backend target here violates the disconnected state.
# Preconditions: included by the top-level CMakeLists after common/.
# Phase: application native build.
# Note: 43499-Glued, 43284-Glued, and 98260 remain source-only historical candidates.
# Reconnection requires a new approved backend design; no Gradle property can bypass
# this boundary.
