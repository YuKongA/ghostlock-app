/** @file tests/snitch_owner_test.cpp
 *  The owner's sequence, with a seam of our own in place of the device's.
 *
 *  The first two steps are pure: building the shape and laying out the region need no platform call,
 *  so their refusals are pinned directly. The steps after them are reached with a recording seam that
 *  never touches the kernel, which is what the seam parameter exists for -- the device header includes
 *  linux/futex.h and this file has to compile on the host.
 *
 *  The contract being checked throughout is the one the owner promises: a refusal anywhere leaves the
 *  canonical failure behind, never a partial discovery. A test that only checked the verdict would
 *  miss exactly the case that matters.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "backend/cve_2026_43499/leak/collision_buckets.hpp"
#include "backend/cve_2026_43499/leak/snitch_owner.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::leak::CollisionBuckets;
    using ghostlock::backend::cve_2026_43499::leak::MapResult;
    using ghostlock::backend::cve_2026_43499::leak::OwnerVerdict;
    using ghostlock::backend::cve_2026_43499::leak::RegionInputs;
    using ghostlock::backend::cve_2026_43499::leak::run_snitch_owner;
    using ghostlock::backend::cve_2026_43499::leak::ScanRange;
    using ghostlock::backend::cve_2026_43499::leak::ScanStrides;
    using ghostlock::backend::cve_2026_43499::leak::SeamResult;
    using ghostlock::backend::cve_2026_43499::leak::SnitchInputs;
    using ghostlock::backend::cve_2026_43499::leak::SnitchPatience;
    using ghostlock::backend::cve_2026_43499::leak::ThreadEntry;
    using ghostlock::backend::cve_2026_43499::leak::ThreadResult;
    using ghostlock::backend::cve_2026_43499::memory::SeamResult;
    using ghostlock::contract::discovery_is_complete;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    /** A seam that refuses every platform call. Nothing here reaches the kernel. */
    struct RefusingSeam final {
        static MapResult map_shared(std::size_t) noexcept { return MapResult{nullptr, -1}; }
        static SeamResult unmap(void *, std::size_t) noexcept { return SeamResult{-1, -1}; }
        static SeamResult futex_wait_private(std::uint32_t *, std::uint32_t) noexcept {
            return SeamResult{-1, -1};
        }
        static SeamResult futex_wait_private_until(std::uint32_t *, std::uint32_t,
                                                   std::int64_t) noexcept {
            return SeamResult{-1, -1};
        }
        static SeamResult futex_wake_private(std::uint32_t *, std::int32_t) noexcept {
            return SeamResult{-1, -1};
        }
        static ThreadResult start_thread(ThreadEntry, void *) noexcept {
            return ThreadResult{0U, -1};
        }
        static SeamResult join_thread(unsigned long) noexcept { return SeamResult{-1, -1}; }
        static SeamResult page_size() noexcept { return SeamResult{4096, 0}; }
        static SeamResult open_memfd(std::uint64_t) noexcept { return SeamResult{-1, -1}; }
        static SeamResult close_fd(std::int32_t) noexcept { return SeamResult{-1, -1}; }
        static SeamResult socket_pair(std::int32_t *) noexcept { return SeamResult{-1, -1}; }
        static SeamResult set_socket_buffer(std::int32_t, std::int32_t) noexcept {
            return SeamResult{-1, -1};
        }
        static SeamResult set_nonblocking(std::int32_t) noexcept { return SeamResult{-1, -1}; }
        static SeamResult send_buffer(std::int32_t, std::size_t) noexcept { return SeamResult{-1, -1}; }
        static SeamResult fork_child() noexcept { return SeamResult{-1, -1}; }
        static SeamResult stop_child(std::uint64_t) noexcept { return SeamResult{-1, -1}; }
        static SeamResult yield_now() noexcept { return SeamResult{-1, -1}; }
        static SeamResult pin_to_processor(std::int32_t) noexcept { return SeamResult{-1, -1}; }
        /** A child ends itself rather than returning up the stack, so this one returns void. */
        static void exit_child() noexcept {}
    };

    /** Inputs that pass the shape step, so a test can reach the step after it. */
    SnitchInputs good_inputs() {
        SnitchInputs in{};
        in.page_size = 4096U;
        in.mm_struct_size = 0x1000U;
        in.mm_slab_order = 0U;
        in.collisions = 2U;
        in.thread_count = 1U;
        in.possible_cpus = 1U;
        return in;
    }

    RegionInputs good_region() {
        RegionInputs r{};
        r.header_bytes = 64U;
        r.collisions = 2U;
        r.thread_count = 1U;
        r.total_pokes = 16U;
        r.page_size = 4096U;
        r.word_bytes = 4U;
        r.handle_bytes = 4U;
        return r;
    }

    struct Call final {
        SnitchInputs inputs{};
        RegionInputs region{};
        SnitchPatience patience{};
        ScanStrides strides{};
        ScanRange range{};
        CollisionBuckets buckets{};
        std::uint64_t addresses[2U] = {1U, 2U};
        std::uint32_t words[2U] = {1U, 2U};
        std::uint32_t *word_ptrs[2U] = {&words[0U], &words[1U]};
    };

    void a_shape_without_its_facts_is_refused_and_leaves_nothing_behind() {
        Call call;
        SnitchInputs bad = good_inputs();
        bad.page_size = 0U;   /* the page size is measured on the device; zero means it was not */
        const auto out = run_snitch_owner<RefusingSeam, CollisionBuckets>(
            bad, call.region, call.patience, 0U, call.word_ptrs, 2U, call.strides, call.range, false,
            call.buckets, call.addresses, 2U, 2U);
        expect(out.status == OwnerVerdict::SetupRefused, "a shape without a page size is refused");
        expect(!out.discovery.ok, "and the discovery is not ok");
        expect(!discovery_is_complete(out.discovery), "and it is not a complete answer");
        expect(out.discovery.mm_struct == 0U && out.discovery.init_task == 0U &&
                   out.discovery.target_task == 0U && out.discovery.kaslr_base == 0U,
               "and nothing partial is exposed");
    }

    void a_region_that_cannot_be_laid_out_is_refused_the_same_way() {
        Call call;
        RegionInputs empty{};
        const auto out = run_snitch_owner<RefusingSeam, CollisionBuckets>(
            good_inputs(), empty, call.patience, 0U, call.word_ptrs, 2U, call.strides, call.range,
            false, call.buckets, call.addresses, 2U, 2U);
        expect(out.status == OwnerVerdict::RegionRefused, "a region with no sizes is refused");
        expect(!out.discovery.ok && out.discovery.mm_struct == 0U,
               "and the discovery is the canonical failure, not a partial one");
    }

    void the_poke_step_is_reached_and_its_refusal_is_named() {
        Call call;
        call.patience.wake_rounds = 4U;
        const auto out = run_snitch_owner<RefusingSeam, CollisionBuckets>(
            good_inputs(), good_region(), call.patience, 0U, call.word_ptrs, 2U, call.strides,
            call.range, false, call.buckets, call.addresses, 2U, 2U);
        expect(out.setup == ghostlock::backend::cve_2026_43499::leak::SnitchSetupVerdict::Ok,
               "the shape step passed, so the poke step was reached");
        expect(out.region == ghostlock::backend::cve_2026_43499::leak::RegionVerdict::Ok,
               "and so did the region step");
        /* Pin the poke verdict itself, not only that something refused: a seam that refuses every
         *  platform call must be refused as a failing call, and the owner must carry that verdict
         *  rather than a verdict of its own invention. Weakening this to "not the shape refusal"
         *  would have let a relaxed check pass unnoticed, which is how this assertion was written
         *  the first time. */
        expect(out.poke == ghostlock::backend::cve_2026_43499::leak::ScanVerdict::CallFailed,
               "the poke verdict is the seam's own, carried through");
        expect(out.status == OwnerVerdict::CallFailed,
               "and the owner's status is that same refusal");
        expect(!out.discovery.ok && out.discovery.mm_struct == 0U,
               "and still nothing partial is exposed");
        std::printf("snitch owner: setup=%s region=%s poke=%u\n",
                    ghostlock::backend::cve_2026_43499::leak::snitch_setup_verdict_name(out.setup),
                    ghostlock::backend::cve_2026_43499::leak::region_verdict_name(out.region),
                    static_cast<unsigned>(out.poke));
    }
}  // namespace

int main() {
    std::printf("snitch owner: the shape step\n");
    a_shape_without_its_facts_is_refused_and_leaves_nothing_behind();
    std::printf("snitch owner: the region step\n");
    a_region_that_cannot_be_laid_out_is_refused_the_same_way();
    std::printf("snitch owner: the poke step\n");
    the_poke_step_is_reached_and_its_refusal_is_named();
    if (g_failures != 0U) {
        std::printf("snitch owner: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("snitch owner: PASS (each step refuses by name and leaves nothing partial)\n");
    return 0;
}
