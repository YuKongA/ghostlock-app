/** @file tests/snitch_scan_test.cpp
 *  The wake protocol over a fake cohort: one that pokes, one that never does, one that
 *  loses a fork. The fake maps a host buffer and pokes the counter when it is woken,
 *  which is what a real child does, so the loop under test is the loop that will run.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "backend/cve_2026_43499/leak/snitch_scan.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::leak::MapResult;
    using ghostlock::backend::cve_2026_43499::leak::RegionInputs;
    using ghostlock::backend::cve_2026_43499::leak::run_poke_protocol;
    using ghostlock::backend::cve_2026_43499::leak::ScanOutcome;
    using ghostlock::backend::cve_2026_43499::leak::scan_verdict_name;
    using ghostlock::backend::cve_2026_43499::leak::ScanVerdict;
    using ghostlock::backend::cve_2026_43499::leak::SnitchPatience;
    using ghostlock::backend::cve_2026_43499::leak::SnitchSetup;
    using ghostlock::backend::cve_2026_43499::leak::ThreadEntry;
    using ghostlock::backend::cve_2026_43499::leak::ThreadResult;
    using ghostlock::backend::cve_2026_43499::memory::SeamResult;

    constexpr std::size_t kCounterOffset = 8U;
    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    struct FakeSeam final {
        static unsigned char buffer[65536];
        static std::size_t forks, kills, wakes, unmaps, pokes_per_wake;
        static std::uint32_t *woken[16];
        static bool fail_fork;
        static void reset() noexcept {
            std::memset(buffer, 0, sizeof(buffer));
            forks = kills = wakes = unmaps = 0U;
            for (std::size_t i = 0U; i < 16U; ++i) { woken[i] = nullptr; }
            pokes_per_wake = 1U;
            fail_fork = false;
        }
        static MapResult map_shared(std::size_t bytes) noexcept {
            if (bytes > sizeof(buffer)) { return MapResult{nullptr, 12}; }
            return MapResult{buffer, 0};
        }
        static SeamResult unmap(void *, std::size_t) noexcept { ++unmaps; return SeamResult{0, 0}; }
        static SeamResult futex_wait_private(std::uint32_t *, std::uint32_t) noexcept { return SeamResult{0, 0}; }
        static SeamResult futex_wait_private_until(std::uint32_t *, std::uint32_t, std::int64_t) noexcept { return SeamResult{0, 0}; }
        static SeamResult futex_wake_private(std::uint32_t *word, std::int32_t) noexcept {
            if (wakes < 16U) { woken[wakes] = word; }
            ++wakes;
            auto *count = reinterpret_cast<std::uint32_t *>(buffer + kCounterOffset);
            *count += static_cast<std::uint32_t>(pokes_per_wake);
            return SeamResult{1, 0};
        }
        static SeamResult fork_child() noexcept {
            if (fail_fork && forks > 0U) { return SeamResult{-1, 11}; }
            ++forks;
            return SeamResult{static_cast<std::int64_t>(1000U + forks), 0};
        }
        static SeamResult stop_child(std::uint64_t) noexcept { ++kills; return SeamResult{0, 0}; }
        static SeamResult yield_now() noexcept { return SeamResult{0, 0}; }
        static SeamResult pin_to_processor(std::int32_t) noexcept { return SeamResult{0, 0}; }
        static void exit_child() noexcept {}
        static ThreadResult start_thread(ThreadEntry, void *) noexcept { return ThreadResult{1U, 0}; }
        static SeamResult join_thread(unsigned long) noexcept { return SeamResult{0, 0}; }
        static SeamResult open_memfd(std::uint64_t) noexcept { return SeamResult{1, 0}; }
        static SeamResult close_fd(std::int32_t) noexcept { return SeamResult{0, 0}; }
        static SeamResult socket_pair(std::int32_t *) noexcept { return SeamResult{0, 0}; }
        static SeamResult set_socket_buffer(std::int32_t, std::int32_t) noexcept { return SeamResult{0, 0}; }
        static SeamResult set_nonblocking(std::int32_t) noexcept { return SeamResult{0, 0}; }
        static SeamResult send_buffer(std::int32_t, std::size_t) noexcept { return SeamResult{0, 0}; }
        static SeamResult page_size() noexcept { return SeamResult{4096, 0}; }
    };
    unsigned char FakeSeam::buffer[65536] = {};
    std::size_t FakeSeam::forks = 0U, FakeSeam::kills = 0U, FakeSeam::wakes = 0U;
    std::size_t FakeSeam::unmaps = 0U, FakeSeam::pokes_per_wake = 1U;
    std::uint32_t *FakeSeam::woken[16] = {};
    bool FakeSeam::fail_fork = false;

    std::uint32_t kWords[2] = {0U, 0U};
    std::uint32_t *kWordPointers[2] = {&kWords[0], &kWords[1]};

    SnitchSetup setup() {
        SnitchSetup made;
        made.collisions = 2U;
        made.futex_table_size = 256U;
        return made;
    }
    RegionInputs inputs() {
        RegionInputs made;
        made.header_bytes = 64U;
        made.collisions = 2U;
        made.thread_count = 2U;
        made.total_pokes = 2048U;
        made.page_size = 4096U;
        made.word_bytes = 8U;
        made.handle_bytes = 8U;
        return made;
    }
    SnitchPatience rounds(std::uint32_t n) {
        SnitchPatience made;
        made.wake_rounds = n;
        return made;
    }

    void a_cohort_that_pokes() {
        FakeSeam::reset();
        FakeSeam::pokes_per_wake = 512U;
        const ScanOutcome outcome = run_poke_protocol<FakeSeam>(setup(), inputs(), rounds(64U), kCounterOffset, kWordPointers, 2U);
        expect(outcome.status == ScanVerdict::Poked, "a cohort that catches up ends the protocol");
        expect(outcome.pokes_wanted == 2048U, "with the total it waited for");
        expect(outcome.rounds < 64U, "in fewer rounds than it was allowed");
        expect(FakeSeam::forks == 2U && FakeSeam::kills == 0U, "one child per collision, none stopped");
        expect(outcome.region != nullptr, "and the region is the caller's to unmap");
        /* Each wake has to name a child's own word: waking the wrong one leaves a child
         *  waiting and the cohort never catches up. */
        expect(FakeSeam::wakes >= 2U, "every child of the cohort was woken");
        expect(FakeSeam::woken[0] == kWordPointers[0] && FakeSeam::woken[1] == kWordPointers[1],
               "and each of them on its own word, in the cohort's order");
    }

    void a_cohort_that_never_does() {
        FakeSeam::reset();
        FakeSeam::pokes_per_wake = 0U;
        const ScanOutcome outcome = run_poke_protocol<FakeSeam>(setup(), inputs(), rounds(8U), kCounterOffset, kWordPointers, 2U);
        expect(outcome.status == ScanVerdict::NoPokes, "a cohort that never catches up is given up on");
        expect(outcome.rounds == 8U, "after exactly the rounds allowed");
        expect(FakeSeam::kills == 2U, "and its children are stopped");
        expect(std::string_view(scan_verdict_name(ScanVerdict::NoPokes)) == std::string_view("plan-missing"),
               "under the name for a plan that did not happen");
    }

    void a_fork_that_fails() {
        FakeSeam::reset();
        FakeSeam::fail_fork = true;
        const ScanOutcome outcome = run_poke_protocol<FakeSeam>(setup(), inputs(), rounds(8U), kCounterOffset, kWordPointers, 2U);
        expect(outcome.status == ScanVerdict::CallFailed, "a fork that fails stops the attempt");
        expect(outcome.error == 11, "and the errno comes back with it");
        expect(FakeSeam::kills == 1U && FakeSeam::unmaps == 1U, "the child is stopped and the region goes back");
        expect(outcome.region == nullptr, "with nothing left for the caller to unmap");
    }

    void what_is_refused_before_anything_is_mapped() {
        FakeSeam::reset();
        RegionInputs broken = inputs();
        broken.total_pokes = 0U;
        expect(run_poke_protocol<FakeSeam>(setup(), broken, rounds(8U), kCounterOffset, kWordPointers, 2U).status ==
                   ScanVerdict::GeometryMissing,
               "a region with nowhere to record pokes is refused");
        expect(FakeSeam::forks == 0U && FakeSeam::wakes == 0U, "before anything is mapped or forked");
        expect(run_poke_protocol<FakeSeam>(setup(), inputs(), rounds(0U), kCounterOffset, kWordPointers, 2U).status ==
                   ScanVerdict::GeometryMissing,
               "and so is a protocol with no rounds to run");
        expect(run_poke_protocol<FakeSeam>(setup(), inputs(), rounds(8U), 40000U, kWordPointers, 2U).status ==
                   ScanVerdict::GeometryMissing,
               "as is a counter that lies outside the region");
        expect(run_poke_protocol<FakeSeam>(setup(), inputs(), rounds(8U), kCounterOffset, nullptr, 2U).status ==
                   ScanVerdict::GeometryMissing,
               "a cohort with no words to wake is refused, not woken at nothing");
        expect(run_poke_protocol<FakeSeam>(setup(), inputs(), rounds(8U), kCounterOffset, kWordPointers, 1U).status ==
                   ScanVerdict::GeometryMissing,
               "and one word short of the cohort is refused too");
    }
}  // namespace

int main() {
    std::printf("snitch scan: a cohort that pokes\n");
    a_cohort_that_pokes();
    std::printf("snitch scan: a cohort that does not\n");
    a_cohort_that_never_does();
    std::printf("snitch scan: a fork that fails\n");
    a_fork_that_fails();
    std::printf("snitch scan: what is refused\n");
    what_is_refused_before_anything_is_mapped();
    if (g_failures != 0U) {
        std::printf("snitch scan: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("snitch scan: PASS (pokes finish it, silence ends it, failure tidies up)\n");
    return 0;
}
