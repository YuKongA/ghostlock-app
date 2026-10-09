/** @file tests/leak_walk_test.cpp
 *  The sequence of addresses the leak search tries, pinned exactly.
 *
 *  The nesting is the part worth pinning: an inner walk stops at the next step of the
 *  walk outside it, so a slab never runs past its coarse block and a candidate never
 *  runs past its slab. An off-by-one there would still find the object eventually, only
 *  after looking where it should not have, and no device run would say which -- so the
 *  whole sequence is enumerated here instead, on strides small enough to count.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "backend/cve_2026_43499/leak/leak_walk.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::leak::kCanonicalTag;
    using ghostlock::backend::cve_2026_43499::leak::kTagSweepCount;
    using ghostlock::backend::cve_2026_43499::leak::LeakProbe;
    using ghostlock::backend::cve_2026_43499::leak::ScanRange;
    using ghostlock::backend::cve_2026_43499::leak::ScanStrides;
    using ghostlock::backend::cve_2026_43499::leak::walk_leak_candidates;
    using ghostlock::backend::cve_2026_43499::leak::walk_verdict_name;
    using ghostlock::backend::cve_2026_43499::leak::WalkVerdict;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** Records what it was shown, and stops after a set number of probes. */
    struct Recorder final {
        LeakProbe seen[64] = {};
        std::size_t count = 0U;
        std::size_t stop_after = 0U;

        bool on_probe(const LeakProbe &probe) noexcept {
            if (count < 64U) {
                seen[count] = probe;
            }
            ++count;
            if (stop_after != 0U && count >= stop_after) {
                return false;
            }
            return true;
        }
    };

    ScanStrides small() {
        ScanStrides strides;
        strides.coarse = 4096U;
        strides.slab = 1024U;
        strides.candidate = 256U;
        return strides;
    }

    void the_sequence_is_the_nesting() {
        Recorder recorder;
        ScanRange range;
        range.start = 0U;
        range.end = 8192U;
        const WalkVerdict verdict =
            walk_leak_candidates(small(), range, false, recorder);
        expect(verdict == WalkVerdict::Ok, "a walk over a whole range ends on its own");
        /* Two coarse blocks, four slabs each, four candidates each, canonical only. */
        expect(recorder.count == 32U, "two blocks of four slabs of four candidates");
        expect(recorder.seen[0].address == 0U && recorder.seen[0].tag == kCanonicalTag,
               "the first probe is the start of the range, canonical");
        expect(recorder.seen[1].address == 256U, "then the next candidate in that slab");
        expect(recorder.seen[4].address == 1024U,
               "and the slab walk moves on after its candidates, not past its block");
        expect(recorder.seen[16].address == 4096U, "the second coarse block starts at its own boundary");
        expect(recorder.seen[31].address == 4096U + 3U * 1024U + 3U * 256U,
               "and the last probe is the last candidate of the last slab");
        expect(recorder.seen[31].address < range.end, "which is still inside the range");
    }

    void the_tags_follow_the_canonical_pass() {
        Recorder recorder;
        ScanRange range;
        range.start = 0U;
        range.end = 4096U;
        (void) walk_leak_candidates(small(), range, true, recorder);
        expect(recorder.count == 16U * (1U + kTagSweepCount),
               "sweeping tries the canonical pass and every sweep tag for each address");
        expect(recorder.seen[0].tag == kCanonicalTag, "canonical comes first");
        for (std::size_t step = 0U; step < kTagSweepCount; ++step) {
            expect(recorder.seen[1U + step].address == 0U,
                   "and the sweep stays on the same address while it runs");
            expect(recorder.seen[1U + step].tag != kCanonicalTag,
                   "never repeating the canonical pass");
        }
    }

    void refusing_and_stopping() {
        Recorder recorder;
        ScanRange range;
        range.start = 0U;
        range.end = 8192U;
        ScanStrides broken = small();
        broken.slab = 0U;
        expect(walk_leak_candidates(broken, range, false, recorder) == WalkVerdict::GeometryMissing,
               "a stride of zero is refused rather than walked");
        expect(recorder.count == 0U, "and nothing is tried");
        expect(std::string_view(walk_verdict_name(WalkVerdict::GeometryMissing)) ==
                   std::string_view("geometry-missing"),
               "under the geometry name");
        Recorder stopping;
        stopping.stop_after = 3U;
        expect(walk_leak_candidates(small(), range, false, stopping) == WalkVerdict::Stopped,
               "a sink that says stop ends the walk");
        expect(stopping.count == 3U, "at the probe it refused");
    }

    void a_walk_that_cannot_advance() {
        /* The shape of the mistake that was found by injecting it: a bound that comes
         *  back at or behind the current address. Before this guard the walk spun at a
         *  hundred percent of a processor, which no assertion after it can report. */
        Recorder recorder;
        ScanRange backwards;
        backwards.start = 4096U;
        backwards.end = 1024U;
        expect(walk_leak_candidates(small(), backwards, false, recorder) == WalkVerdict::GeometryMissing,
               "a range that does not contain its own start is refused, not walked");
        expect(recorder.count == 0U, "and nothing is tried");
        ScanRange precise;
        precise.start = 0U;
        precise.end = 512U;
        Recorder narrow;
        (void) walk_leak_candidates(small(), precise, false, narrow);
        expect(narrow.count == 2U,
               "a range smaller than a coarse step still walks its own candidates once");
        expect(narrow.seen[0].address == 0U && narrow.seen[1].address == 256U,
               "and stops at the range end rather than at the coarse boundary");
    }
}  // namespace

int main() {
    std::printf("leak walk: the nesting\n");
    the_sequence_is_the_nesting();
    std::printf("leak walk: the tags\n");
    the_tags_follow_the_canonical_pass();
    std::printf("leak walk: refusing and stopping\n");
    refusing_and_stopping();
    std::printf("leak walk: a walk that cannot advance\n");
    a_walk_that_cannot_advance();
    if (g_failures != 0U) {
        std::printf("leak walk: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("leak walk: PASS (the nesting, the tags, and where it stops)\n");
    return 0;
}
