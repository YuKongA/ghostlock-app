#ifndef GHOSTLOCK_BACKEND_CVE_2026_43499_STEPS_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43499_STEPS_HPP

#include "backend/cve_2026_43499/stage_types.hpp"
#include "contract/identity.hpp"
#include "contract/stage_result.hpp"
#include "contract/step_catalog.hpp"

#include <cstddef>
#include <iterator>

namespace ghostlock::session {
    struct CoreSession;
} // namespace ghostlock::session

namespace ghostlock::backend {
    /* Backend-private step vocabularies (ADR-0004 R18). Each is a compile-time
     * policy whose run<M> is the W1 -> W2 (-> W3) sequence for one route M.
     * Adding a step set is a new type here plus a catalog triple; the pipeline
     * never branches on the kind.
     *
     * W1W2 is the shell / kernel-spawned entry: there is no seccomp filter to
     * clear, so W3 is not part of this type at all (it is never instantiated for
     * a W1W2 route). W1W3 keeps the original app-descendant sequence. */
    struct W1W3Steps final {
        static constexpr contract::StepSetKind kind = contract::StepSetKind::W1W3;

        /* The execution order run<M>() follows, as data: it is the export
         * authority for stepset-steps.tsv (M3 reads the sequence per step set).
         * The static_asserts below bind it to the contract alias table, so the
         * executor order, the catalogue and the exported manifest cannot drift.
         * Design doc 4.1: W1W3 == [w1, w2, w3]. */
        static constexpr contract::StepId kSteps[] = {contract::StepId::W1,
                                                      contract::StepId::W2,
                                                      contract::StepId::W3};

        template <class M>
        [[nodiscard]] static contract::StageResult run(session::CoreSession &session,
                                                      VictimChain &chain);
    };

    struct W1W2Steps final {
        static constexpr contract::StepSetKind kind = contract::StepSetKind::W1W2;

        /* Design doc 4.1: W1W2 == [w1, w2] (no seccomp filter to clear). */
        static constexpr contract::StepId kSteps[] = {contract::StepId::W1,
                                                      contract::StepId::W2};

        template <class M>
        [[nodiscard]] static contract::StageResult run(session::CoreSession &session,
                                                      VictimChain &chain);
    };

    namespace step_detail {
        /* A step set's declared order must equal the contract alias table
         * (contract/step_catalog.hpp) item by item: reordering either side fails
         * the build instead of shipping a plan that differs from the exported
         * stepset-steps.tsv. */
        template <class Steps>
        [[nodiscard]] constexpr bool order_matches_alias() noexcept {
            const contract::StepSetAlias *alias = contract::step_set_alias(Steps::kind);
            if (alias == nullptr) return false;
            if (alias->steps.size() != std::size(Steps::kSteps)) return false;
            for (std::size_t i = 0; i < alias->steps.size(); ++i) {
                if (alias->steps[i] != Steps::kSteps[i]) return false;
            }
            return true;
        }
    } // namespace step_detail

    static_assert(step_detail::order_matches_alias<W1W3Steps>(),
                  "W1W3Steps::kSteps must equal the contract alias table for W1W3");
    static_assert(step_detail::order_matches_alias<W1W2Steps>(),
                  "W1W2Steps::kSteps must equal the contract alias table for W1W2");
} // namespace ghostlock::backend

#endif
