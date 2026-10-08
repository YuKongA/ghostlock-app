#ifndef GHOSTLOCK_CONTRACT_STAGE_RESULT_HPP
#define GHOSTLOCK_CONTRACT_STAGE_RESULT_HPP

namespace ghostlock::contract {
    /* Outcome of one orchestration stage. Failed maps to exit code 1, Continue
     * proceeds to the next stage and Done stops with exit code 0 (diagnostics). */
    enum class StageResult {
        Failed,
        Continue,
        Done,
    };
} // namespace ghostlock::contract

#endif
