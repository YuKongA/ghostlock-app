#pragma once
/** @file script/stage_gate.hpp
 *  A6-2: what happens to a staged plan between recording and the admission gate.
 *
 *  Layering note: this layer may not include pipeline/ (the include firewall
 *  keeps the whitelist empty), so the gate itself is called by the entry, which
 *  owns the sequence: run the script once, check the recording here, hand the
 *  host-built plan to pipeline::plan_gate, then fold the verdict back in by name.
 *
 *  The two checks are deliberately separate: this file checks what the script
 *  produced against the table the script was bound from; the gate checks the
 *  host's plan declaration against availability, geometry, capability and order.
 *  A refusal name is never invented here -- the gate's own names are folded in.
 */
#include <cstddef>
#include <string_view>

#include "contract/script_surface.hpp"
#include "script/staged_plan.hpp"

namespace ghostlock::script {
    /** The outcome of validating one staged plan. */
    struct StageVerdict final {
        bool ok = false;
        /** A name from the single rejection table (never an invented string). */
        const char *reason = "plan-missing";
        /** The entry the refusal points at, when it points at one. */
        std::size_t index = 0U;
    };

    /** Check the recording against the table it was bound from: every entry's
     *  argument count must fit the row's declared list. Rows are addressed by
     *  index into kSurface, so a name outside the table cannot appear here. */
    [[nodiscard]] inline StageVerdict check_surface_entries(const StagedPlan &plan) noexcept {
        StageVerdict verdict{};
        for (std::size_t i = 0U; i < plan.size(); ++i) {
            const StagedEntry &entry = plan.at(i);
            if (entry.surface_index >= contract::kSurfaceCount) {
                verdict.ok = false;
                verdict.reason = "surface-unknown-name";
                verdict.index = i;
                return verdict;
            }
            /* Rows that declare no arguments are free-form by design (the
             * acquiring row carries a backend id the table does not spell), so
             * only rows with a declared list are checked against it. */
            const std::size_t declared = contract::kSurface[entry.surface_index].arg_count;
            if (declared > 0U && entry.arg_count > declared) {
                verdict.ok = false;
                verdict.reason = "param-invalid";
                verdict.index = i;
                return verdict;
            }
            /* Text arguments: max is a refusal bound, not a truncation point. A value over it
             * is refused here with the code this gate already spells, so nothing downstream has
             * to carry an over-long string or a second vocabulary for saying so. */
            if (declared > 0U) {
                const contract::ArgSpec *specs = contract::kSurface[entry.surface_index].args;
                const std::size_t checked =
                        entry.arg_count < declared ? entry.arg_count : declared;
                for (std::size_t a = 0U; a < checked; ++a) {
                    if (entry.args[a].kind == contract::ArgKind::Str &&
                        static_cast<std::uint64_t>(entry.args[a].str_length) > specs[a].max) {
                        verdict.ok = false;
                        verdict.reason = "param-invalid";
                        verdict.index = i;
                        return verdict;
                    }
                }
            }
        }
        verdict.ok = true;
        verdict.reason = "ok";
        return verdict;
    }

    /** Fold a refusal raised elsewhere (the admission gate) into the plan, so a
     *  single refusal precedes execution and nothing runs after it. */
    [[nodiscard]] inline bool fold_refusal(StagedPlan &plan, std::string_view reason) noexcept {
        /* refuse() carries the record() failure return, so it always reports
         * false; here the question is whether the plan was still clean. */
        if (!plan.ok()) {
            return false;
        }
        (void)plan.refuse(reason);
        return true;
    }
}  // namespace ghostlock::script
