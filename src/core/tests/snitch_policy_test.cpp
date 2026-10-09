/** @file tests/snitch_policy_test.cpp
 *  The search behind the preparation's polls.
 *
 *  Two properties are worth pinning. Starting twice runs the search once: a preparation that
 *  asked again would be asking for a second cohort of children. And a run that never reached
 *  its walk reports no collision rather than an address of zero dressed as one, because the
 *  preparation treats a zero base as a failure of its own and the two must not be confused.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "backend/cve_2026_43499/leak/snitch_policy.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::leak::kCanonicalTag;
    using ghostlock::backend::cve_2026_43499::leak::MapResult;
    using ghostlock::backend::cve_2026_43499::leak::RegionInputs;
    using ghostlock::backend::cve_2026_43499::leak::ScanRange;
    using ghostlock::backend::cve_2026_43499::leak::ScanStrides;
    using ghostlock::backend::cve_2026_43499::leak::SnitchPatience;
    using ghostlock::backend::cve_2026_43499::leak::SnitchPolicy;
    using ghostlock::backend::cve_2026_43499::leak::SnitchPolicyArgs;
    using ghostlock::backend::cve_2026_43499::leak::SnitchSetup;
    using ghostlock::backend::cve_2026_43499::leak::tagged_candidate;
    using ghostlock::backend::cve_2026_43499::leak::ThreadEntry;
    using ghostlock::backend::cve_2026_43499::leak::ThreadResult;
    using ghostlock::backend::cve_2026_43499::memory::SeamResult;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    struct FakeSeam final {
        static unsigned char buffer[65536];
        static std::size_t forks, wakes, pokes_per_wake;
        static void reset() noexcept {
            std::memset(buffer, 0, sizeof(buffer));
            forks = wakes = 0U;
            pokes_per_wake = 512U;
        }
        static MapResult map_shared(std::size_t bytes) noexcept {
            if (bytes > sizeof(buffer)) { return MapResult{nullptr, 12}; }
            return MapResult{buffer, 0};
        }
        static SeamResult unmap(void *, std::size_t) noexcept { return SeamResult{0, 0}; }
        static SeamResult futex_wait_private(std::uint32_t *, std::uint32_t) noexcept { return SeamResult{0, 0}; }
        static SeamResult futex_wait_private_until(std::uint32_t *, std::uint32_t, std::int64_t) noexcept {
            return SeamResult{0, 0};
        }
        static SeamResult futex_wake_private(std::uint32_t *, std::int32_t) noexcept {
            ++wakes;
            auto *count = reinterpret_cast<std::uint32_t *>(buffer + 128U);
            *count += static_cast<std::uint32_t>(pokes_per_wake);
            return SeamResult{1, 0};
        }
        static SeamResult fork_child() noexcept { ++forks; return SeamResult{static_cast<std::int64_t>(1000U + forks), 0}; }
        static SeamResult stop_child(std::uint64_t) noexcept { return SeamResult{0, 0}; }
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
    std::size_t FakeSeam::forks = 0U, FakeSeam::wakes = 0U, FakeSeam::pokes_per_wake = 512U;

    struct Buckets final {
        std::uint64_t target = 0U;
        bool has_target = false;
        [[nodiscard]] std::uint64_t bucket(std::uint64_t address, std::uint64_t candidate) const noexcept {
            if (has_target && candidate == target) { return 7U; }
            return 3U + ((address / 0x1000U) % 2U);
        }
    };

    std::uint32_t kWords[2] = {0U, 0U};
    std::uint32_t *kWordPointers[2] = {&kWords[0], &kWords[1]};
    std::uint64_t kAddresses[2] = {0x1000U, 0x2000U};

    SnitchPolicyArgs args() {
        SnitchPolicyArgs made{};
        made.setup.collisions = 2U;
        made.setup.thread_count = 1U;
        made.setup.futex_table_size = 256U;
        made.region.header_bytes = 256U;
        made.region.collisions = 2U;
        made.region.thread_count = 2U;
        made.region.total_pokes = 1024U;
        made.region.page_size = 4096U;
        made.region.word_bytes = 8U;
        made.region.handle_bytes = 8U;
        made.patience.wake_rounds = 8U;
        made.counter_offset = 128U;
        made.words = kWordPointers;
        made.word_count = 2U;
        made.strides = ScanStrides{4096U, 1024U, 256U};
        made.range = ScanRange{0U, 8192U};
        made.addresses = kAddresses;
        made.address_count = 2U;
        return made;
    }

    void a_run_answers_the_polls() {
        FakeSeam::reset();
        Buckets buckets;
        buckets.target = tagged_candidate(512U, kCanonicalTag);
        buckets.has_target = true;
        SnitchPolicy<FakeSeam, Buckets> policy;
        policy.args = args();
        policy.buckets = &buckets;
        expect(policy.start(1U).ran, "a run that finds something starts cleanly");
        expect(policy.has_collision(), "and the preparation sees a collision");
        expect(policy.address() == tagged_candidate(512U, kCanonicalTag), "with the address the rule accepted");
        const std::size_t forks_after_first = FakeSeam::forks;
        expect(policy.start(1U).ran, "starting again is answered the same way");
        expect(FakeSeam::forks == forks_after_first, "and does not fork a second cohort");
        policy.reset();
        expect(!policy.has_collision() && policy.address() == 0U, "a reset leaves nothing behind");
    }

    void a_run_without_pokes_finds_nothing() {
        FakeSeam::reset();
        FakeSeam::pokes_per_wake = 0U;
        Buckets buckets;
        buckets.target = tagged_candidate(0U, kCanonicalTag);
        buckets.has_target = true;
        SnitchPolicy<FakeSeam, Buckets> policy;
        policy.args = args();
        policy.buckets = &buckets;
        expect(!policy.start(1U).ran, "a run that never poked does not start cleanly");
        expect(!policy.has_collision(), "and the preparation is told there is no collision");
        expect(policy.address() == 0U,
               "rather than being handed a zero address as though it were one");
    }
}  // namespace

int main() {
    std::printf("snitch policy: a run that answers\n");
    a_run_answers_the_polls();
    std::printf("snitch policy: a run without pokes\n");
    a_run_without_pokes_finds_nothing();
    if (g_failures != 0U) {
        std::printf("snitch policy: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("snitch policy: PASS (one run, then answers the polls)\n");
    return 0;
}
