#pragma once
/** @file contract/stage_result.hpp
 *  What one orchestration stage reports when it returns.
 *
 *  PORTED (2026-10-09) from the retired tree's contract/stage_result.hh. Three values and nothing
 *  else, and the exit codes are part of the contract rather than a caller convention: a failed stage
 *  ends the run with 1, a continuing stage hands over to the next, and a finished stage stops with 0
 *  because the diagnostic run is a success. That mapping is written here because it is the only place
 *  it can be written once.
 *
 *  Do not confuse this with SurfacePhase, which names the phases a *script* call passes through
 *  (Stage / PreWindow / Window / Terminal). This type names the *outcome* of an orchestration stage.
 *  The two are close in name and different in layer, which is exactly why the distinction is spelled
 *  out here rather than left to a reader to infer.
 *
 *  Boundary: this file includes nothing. It depends on no other piece and no other piece's change
 *  can change its meaning.
 */

namespace ghostlock::contract {
    enum class StageResult {
        /** The stage failed. Maps to exit code 1. */
        Failed,
        /** Proceed to the next stage. */
        Continue,
        /** Stop here, successfully. Maps to exit code 0, which is what a diagnostic run wants. */
        Done,
    };
}  // namespace ghostlock::contract
