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
    using ghostlock::backend::cve_2026_43499::memory::OpsResult;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    struct FakeOps final {
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
        static OpsResult unmap(void *, std::size_t) noexcept { return OpsResult{0, 0}; }
        static OpsResult futex_wait_private(std::uint32_t *, std::uint32_t) noexcept { return OpsResult{0, 0}; }
        static OpsResult futex_wait_private_until(std::uint32_t *, std::uint32_t, std::int64_t) noexcept {
            return OpsResult{0, 0};
        }
        static OpsResult futex_wake_private(std::uint32_t *, std::int32_t) noexcept {
            ++wakes;
            auto *count = reinterpret_cast<std::uint32_t *>(buffer + 128U);
            *count += static_cast<std::uint32_t>(pokes_per_wake);
            return OpsResult{1, 0};
        }
        static OpsResult fork_child() noexcept { ++forks; return OpsResult{static_cast<std::int64_t>(1000U + forks), 0}; }
        static OpsResult stop_child(std::uint64_t) noexcept { return OpsResult{0, 0}; }
        static OpsResult yield_now() noexcept { return OpsResult{0, 0}; }
        static OpsResult pin_to_processor(std::int32_t) noexcept { return OpsResult{0, 0}; }
        static void exit_child() noexcept {}
        static ThreadResult start_thread(ThreadEntry, void *) noexcept { return ThreadResult{1U, 0}; }
        static OpsResult join_thread(unsigned long) noexcept { return OpsResult{0, 0}; }
        static OpsResult open_memfd(std::uint64_t) noexcept { return OpsResult{1, 0}; }
        static OpsResult close_fd(std::int32_t) noexcept { return OpsResult{0, 0}; }
        static OpsResult socket_pair(std::int32_t *) noexcept { return OpsResult{0, 0}; }
        static OpsResult set_socket_buffer(std::int32_t, std::int32_t) noexcept { return OpsResult{0, 0}; }
        static OpsResult set_nonblocking(std::int32_t) noexcept { return OpsResult{0, 0}; }
        static OpsResult send_buffer(std::int32_t, std::size_t) noexcept { return OpsResult{0, 0}; }
        /* heap_ops.hpp 的 HeapOps 概念要求「带内容发送」（页面必须走这条路进内核 ✓）：
         *  假 landing 此前缺这一项 ⇒ 概念不满足 ⇒ run_snitch_search 在编译期具名不可调用。
         *  2026-10-14 补齐：与其它调用同形，参数一律忽略。 */
        static OpsResult send_bytes(std::int32_t, const void *, std::size_t) noexcept { return OpsResult{0, 0}; }
        static OpsResult page_size() noexcept { return OpsResult{4096, 0}; }
    };
    unsigned char FakeOps::buffer[65536] = {};
    std::size_t FakeOps::forks = 0U, FakeOps::wakes = 0U, FakeOps::pokes_per_wake = 512U;

    struct Buckets final {
        std::uint64_t target = 0U;
        bool has_target = false;
        [[nodiscard]] std::uint64_t bucket(std::uint64_t address, std::uint64_t candidate) const noexcept {
            if (has_target && candidate == target) { return 7U; }
            return 3U + ((address / 0x1000U) % 2U);
        }
        /** Which bucket an address is in: the fake way of asking the same question the real
         *  rule answers by hashing. */
        [[nodiscard]] std::uint64_t bucket_of(std::uint64_t address) const noexcept {
            return bucket(address, address);
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
        FakeOps::reset();
        Buckets buckets;
        buckets.target = tagged_candidate(512U, kCanonicalTag);
        buckets.has_target = true;
        SnitchPolicy<FakeOps, Buckets> policy;
        policy.args = args();
        policy.buckets = &buckets;
        /* The armed addresses have to be in the target's bucket, and this fake answers a bucket
         *  by asking whether the candidate is the target -- so the only honest way to say they
         *  are in its bucket is for them to be the target. The real rule gets that from the
         *  hash, where two different addresses can share a bucket. */
        const std::uint64_t in_targets_bucket[2] = {buckets.target, buckets.target};
        policy.args.addresses = in_targets_bucket;
        policy.args.address_count = 2U;
        expect(policy.start(1U).ran, "a run that finds something starts cleanly");
        expect(policy.has_collision(), "and the preparation sees a collision");
        expect(policy.address() == tagged_candidate(512U, kCanonicalTag), "with the address the rule accepted");
        const std::size_t forks_after_first = FakeOps::forks;
        expect(policy.start(1U).ran, "starting again is answered the same way");
        expect(FakeOps::forks == forks_after_first, "and does not fork a second cohort");
        policy.reset();
        expect(!policy.has_collision() && policy.address() == 0U, "a reset leaves nothing behind");
    }

    void a_run_without_pokes_finds_nothing() {
        FakeOps::reset();
        FakeOps::pokes_per_wake = 0U;
        Buckets buckets;
        buckets.target = tagged_candidate(0U, kCanonicalTag);
        buckets.has_target = true;
        SnitchPolicy<FakeOps, Buckets> policy;
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
