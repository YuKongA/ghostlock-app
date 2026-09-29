/*
 * GhostLock — ancillary controller translation unit (skeleton).
 *
 * Forces the controller header through the Android compile (and clang-tidy) so
 * the host-safe interface cannot drift from what the device build sees. Stage C
 * adds the behavior bodies that call the backend write primitive and the
 * per-middleware explicit instantiations; for now there is no behavior body and
 * no middleware instance to materialize, so this unit carries only the include.
 */

#include "session/ancillary/ancillary_controller.hpp"
