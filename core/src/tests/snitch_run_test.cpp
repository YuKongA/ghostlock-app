/** @file tests/snitch_run_test.cpp
 *  The whole search, end to end, over fakes: pokes first, then the walk, in that order.
 *
 *  The assertion that carries the weight is the one about a run that never got its
 *  pokes: it must not look at a single address. A search that walks a range whose
 *  collisions never happened finds nothing, and that answer is indistinguishable from a
 *  device where the object is simply elsewhere -- so the two have to be kept apart by
 *  the control flow rather than by a comment.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include "backend/cve_2026_43499/leak/snitch_run.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::leak::kCanonicalTag;
    using ghostlock::backend::cve_2026_43499::leak::MapResult;
    using ghostlock::backend::cve_2026_43499::leak::RegionInputs;
    using ghostlock::backend::cve_2026_43499::leak::run_snitch_search;
    using ghostlock::backend::cve_2026_43499::leak::RunOutcome;
    using ghostlock::backend::cve_2026_43499::leak::run_verdict_name;
    using ghostlock::backend::cve_2026_43499::leak::RunVerdict;
    using ghostlock::backend::cve_2026_43499::leak::ScanRange;
    using ghostlock::backend::cve_2026_43499::leak::ScanStrides;
    using ghostlock::backend::cve_2026_43499::leak::SnitchPatience;
    using ghostlock::backend::cve_2026_43499::leak::SnitchSetup;
    using ghostlock::backend::cve_2026_43499::leak::tagged_candidate;
    using ghostlock::backend::cve_2026_43499::leak::ThreadEntry;
    using ghostlock::backend::cve_2026_43499::leak::ThreadResult;
    using ghostlock::backend::cve_2026_43499::memory::OpsResult;

    constexpr std::size_t kCounterOffset = 8U;
    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    struct FakeOps final {
        static unsigned char buffer[65536];
        static std::size_t forks, kills, wakes, unmaps, pokes_per_wake;
        static void reset() noexcept {
            std::memset(buffer, 0, sizeof(buffer));
            forks = kills = wakes = unmaps = 0U;
            pokes_per_wake = 0U;
        }
        static MapResult map_shared(std::size_t bytes) noexcept {
            if (bytes > sizeof(buffer)) { return MapResult{nullptr, 12}; }
            return MapResult{buffer, 0};
        }
        static OpsResult unmap(void *, std::size_t) noexcept { ++unmaps; return OpsResult{0, 0}; }
        static OpsResult futex_wait_private(std::uint32_t *, std::uint32_t) noexcept { return OpsResult{0, 0}; }
        static OpsResult futex_wait_private_until(std::uint32_t *, std::uint32_t, std::int64_t) noexcept { return OpsResult{0, 0}; }
        static OpsResult futex_wake_private(std::uint32_t *, std::int32_t) noexcept {
            ++wakes;
            auto *count = reinterpret_cast<std::uint32_t *>(buffer + kCounterOffset);
            *count += static_cast<std::uint32_t>(pokes_per_wake);
            return OpsResult{1, 0};
        }
        static OpsResult fork_child() noexcept { ++forks; return OpsResult{static_cast<std::int64_t>(1000U + forks), 0}; }
        static OpsResult stop_child(std::uint64_t) noexcept { ++kills; return OpsResult{0, 0}; }
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
    std::size_t FakeOps::forks = 0U, FakeOps::kills = 0U, FakeOps::wakes = 0U;
    std::size_t FakeOps::unmaps = 0U, FakeOps::pokes_per_wake = 0U;

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
    const std::uint64_t kAddresses[2] = {0x1000U, 0x2000U};

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
    ScanStrides strides() {
        ScanStrides made;
        made.coarse = 4096U;
        made.slab = 1024U;
        made.candidate = 256U;
        return made;
    }
    ScanRange range() {
        ScanRange made;
        made.start = 0U;
        made.end = 8192U;
        return made;
    }

    void a_run_that_finds_it() {
        FakeOps::reset();
        FakeOps::pokes_per_wake = 1024U;
        Buckets buckets;
        buckets.target = tagged_candidate(512U, kCanonicalTag);
        buckets.has_target = true;
        /* The armed addresses have to be in the object's bucket: this fake answers a bucket by
         *  asking whether the candidate is the target, so saying so means being the target. The
         *  real rule gets that from the hash, where two different addresses can share one. */
        const std::uint64_t in_targets_bucket[2] = {buckets.target, buckets.target};
        const RunOutcome outcome = run_snitch_search<FakeOps>(setup(), inputs(), rounds(4U), kCounterOffset,
                                                               kWordPointers, 2U, strides(), range(), false,
                                                               buckets, in_targets_bucket, 2U);
        expect(outcome.status == RunVerdict::Found, "a run that gets its pokes and an answer finds the object");
        expect(outcome.found == tagged_candidate(512U, kCanonicalTag), "at the address the rule accepted");
        expect(outcome.probes == 3U, "after three addresses");
        expect(outcome.region != nullptr, "and the region is the caller's to unmap");
    }

    void a_run_that_never_poked_must_not_search() {
        FakeOps::reset();
        Buckets buckets;
        buckets.target = tagged_candidate(0U, kCanonicalTag);
        buckets.has_target = true;
        const RunOutcome outcome = run_snitch_search<FakeOps>(setup(), inputs(), rounds(4U), kCounterOffset,
                                                               kWordPointers, 2U, strides(), range(), false,
                                                               buckets, kAddresses, 2U);
        expect(outcome.status == RunVerdict::NoPokes, "a run without pokes gives up");
        expect(outcome.probes == 0U,
               "and does not look at a single address, which would look exactly like an object that is elsewhere");
        expect(FakeOps::kills == 2U, "its cohort is stopped");
        expect(std::string_view(run_verdict_name(RunVerdict::NoPokes)) == std::string_view("plan-missing"),
               "under the name for a plan that did not happen");
    }

    void a_run_that_poked_and_found_nothing() {
        FakeOps::reset();
        FakeOps::pokes_per_wake = 1024U;
        Buckets buckets;
        const RunOutcome outcome = run_snitch_search<FakeOps>(setup(), inputs(), rounds(4U), kCounterOffset,
                                                               kWordPointers, 2U, strides(), range(), false,
                                                               buckets, kAddresses, 2U);
        expect(outcome.status == RunVerdict::NotFound, "a run that poked and found nothing says so");
        expect(outcome.probes == 32U, "after the whole range");
    }

    void what_fails_before_the_walk() {
        FakeOps::reset();
        RegionInputs broken = inputs();
        broken.total_pokes = 0U;
        Buckets buckets;
        const RunOutcome refused = run_snitch_search<FakeOps>(setup(), broken, rounds(4U), kCounterOffset,
                                                               kWordPointers, 2U, strides(), range(), false,
                                                               buckets, kAddresses, 2U);
        expect(refused.status == RunVerdict::GeometryMissing, "a region that cannot be laid out stops the run");
        expect(FakeOps::forks == 0U, "before any child is forked");
        FakeOps::reset();
        FakeOps::pokes_per_wake = 1024U;
        const RunOutcome one_address = run_snitch_search<FakeOps>(setup(), inputs(), rounds(4U), kCounterOffset,
                                                                   kWordPointers, 2U, strides(), range(), false,
                                                                   buckets, kAddresses, 1U);
        expect(one_address.status == RunVerdict::GeometryMissing,
               "and a rule with nothing to compare refuses after the pokes");
    }
}  // namespace

int main() {
    std::printf("snitch run: a run that finds it\n");
    a_run_that_finds_it();
    std::printf("snitch run: a run without pokes\n");
    a_run_that_never_poked_must_not_search();
    std::printf("snitch run: poked and nothing\n");
    a_run_that_poked_and_found_nothing();
    std::printf("snitch run: what fails early\n");
    what_fails_before_the_walk();
    if (g_failures != 0U) {
        std::printf("snitch run: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("snitch run: PASS (pokes first, then the walk, and never the other way round)\n");
    return 0;
}
