/*
 * GhostLock — ancillary controller translation unit (skeleton).
 *
 * Forces the neutral controller header through the Android compile (and
 * clang-tidy) so the host-safe interface cannot drift from what the device
 * build sees. The controller is header-only and instantiates nothing here; the
 * behaviors and the registry live at the backend call site.
 */

#include "ancillary/controller.hpp"
