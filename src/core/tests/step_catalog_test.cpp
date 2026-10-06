/* Host test for the step vocabulary (step-queue design doc section 4.1; M1).
 *
 * M1 is declaration-only: this test pins the catalogue, the alias mapping onto
 * today's step sets and the compile-time registration concept. The catalogue
 * invariants themselves are `static_assert`s in the header (a bad row does not
 * compile); the runtime assertions here pin the VALUES (tokens, slots, deps,
 * effects) and the agreement with the existing combination catalogue, so a row
 * edit that keeps the asserts happy but changes the meaning still fails.
 *
 * Compile-time registration (proved by falsification, see the M1 report):
 *   - a catalogue row with no alias/executor coverage fails `static_assert`;
 *   - `StepExecution` accepts a conforming per-step executor and rejects a type
 *     without `step_id` -- M2 binds the real per-step policies to it. */

#include "contract/step_catalog.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

using ghostlock::contract::BackendKind;
using ghostlock::contract::CombinationSpec;
using ghostlock::contract::kCombinationCatalog;
using ghostlock::contract::kStepCatalog;
using ghostlock::contract::kStepCount;
using ghostlock::contract::kStepSetAliasCount;
using ghostlock::contract::StepEffect;
using ghostlock::contract::StepExecution;
using ghostlock::contract::StepId;
using ghostlock::contract::StepSetAlias;
using ghostlock::contract::StepSetKind;
using ghostlock::contract::StepSpec;
using ghostlock::contract::step_is_covered;
using ghostlock::contract::step_set_alias;
using ghostlock::contract::step_spec;
using ghostlock::contract::step_spec_from_token;

namespace {
    /* A conforming per-step executor (the M2 shape) and a type that is not one. */
    struct ProbeExecutor final {
        static constexpr StepId step_id = StepId::W1;
    };
    struct ProbeNotExecutor final {
        static constexpr std::uint8_t slot = 0U;
    };
    static_assert(StepExecution<ProbeExecutor>,
                  "an executor declaring step_id must satisfy StepExecution");
    static_assert(ProbeNotExecutor::slot == 0U);
    static_assert(!StepExecution<ProbeNotExecutor>,
                  "a type without step_id must NOT satisfy StepExecution");
} // namespace

int main() {
    /* ---- catalogue lookup ------------------------------------------------- */
    assert(kStepCount == 4U);
    assert(kStepSetAliasCount == 3U);
    assert(step_spec(StepId::Unknown) == nullptr);
    const StepSpec *w1 = step_spec(StepId::W1);
    assert(w1 != nullptr);
    assert(w1->token == "w1");
    assert(w1->backend == BackendKind::Cve2026_43499);
    assert(w1->slot == 0U);
    assert(w1->deps_mask == 0U);
    assert(!w1->skippable);
    assert(w1->available);
    assert(w1->effect == StepEffect::Selinux);

    const StepSpec *w2 = step_spec(StepId::W2);
    assert(w2 != nullptr && w2->slot == 1U && w2->deps_mask == 0b001U);
    const StepSpec *w3 = step_spec(StepId::W3);
    assert(w3 != nullptr && w3->slot == 2U && w3->deps_mask == 0b011U);
    const StepSpec *pagecache = step_spec(StepId::PageCacheWrite);
    assert(pagecache != nullptr && pagecache->slot == 0U && pagecache->deps_mask == 0U);
    assert(pagecache->backend == BackendKind::Cve2026_43284);
    assert(pagecache->effect == StepEffect::PageCache);

    /* ---- token lookup is exact and backend-scoped ------------------------- */
    assert(step_spec_from_token("w1", BackendKind::Cve2026_43499) == w1);
    assert(step_spec_from_token("w1", BackendKind::Cve2026_43284) == nullptr);
    assert(step_spec_from_token("pagecache_write", BackendKind::Cve2026_43284) == pagecache);
    assert(step_spec_from_token("pagecache_write", BackendKind::Cve2026_43499) == nullptr);
    assert(step_spec_from_token("nope", BackendKind::Cve2026_43499) == nullptr);
    assert(step_spec_from_token("", BackendKind::Cve2026_43499) == nullptr);
    assert(step_spec_from_token(" W1", BackendKind::Cve2026_43499) == nullptr);
    assert(step_spec_from_token("W1", BackendKind::Cve2026_43499) == nullptr);

    /* ---- alias mapping onto today's step sets ---------------------------- */
    const StepSetAlias *w1w3 = step_set_alias(StepSetKind::W1W3);
    assert(w1w3 != nullptr);
    assert(w1w3->backend == BackendKind::Cve2026_43499);
    assert(w1w3->steps.size() == 3U);
    assert(w1w3->steps[0] == StepId::W1 && w1w3->steps[1] == StepId::W2 &&
           w1w3->steps[2] == StepId::W3);
    const StepSetAlias *w1w2 = step_set_alias(StepSetKind::W1W2);
    assert(w1w2 != nullptr && w1w2->steps.size() == 2U);
    assert(w1w2->steps[1] == StepId::W2);
    const StepSetAlias *pcw = step_set_alias(StepSetKind::PageCacheWrite);
    assert(pcw != nullptr && pcw->backend == BackendKind::Cve2026_43284);
    assert(pcw->steps.size() == 1U && pcw->steps[0] == StepId::PageCacheWrite);
    assert(step_set_alias(StepSetKind::Unknown) == nullptr);

    /* Every catalogued step is reachable from at least one alias. */
    for (const StepSpec &spec : kStepCatalog) {
        assert(step_is_covered(spec.id));
    }

    /* ---- agreement with the existing combination catalogue --------------- */
    assert(std::size(kCombinationCatalog) == 12U);
    for (const CombinationSpec &spec : kCombinationCatalog) {
        const StepSetAlias *alias = step_set_alias(spec.steps);
        assert(alias != nullptr);
        assert(alias->backend == spec.backend);
    }

    std::printf("step_catalog_test: ok (%zu steps, %zu aliases)\n", kStepCount,
                kStepSetAliasCount);
    return 0;
}