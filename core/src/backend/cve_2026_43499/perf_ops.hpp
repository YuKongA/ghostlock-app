#pragma once
/** @file backend/cve_2026_43499/perf_ops.hpp
 *  What the sampling primitive needs from the platform, written as a concept.
 *
 *  Opening a sampling event, mapping its ring, starting and stopping it, and closing it:
 *  that is the whole edge, and the retired path needed exactly those. No decisions live
 *  here -- the period, the sample kind and how long to sample are the caller's, and what to
 *  do with the records is the ring walk's business.
 *
 *  A concept rather than a base class, so the device implementation and any host fake are
 *  checked where they are written. The parameters of the open call are given as a small
 *  struct rather than as six arguments in a row, because the kernel's own call takes six and
 *  two of them are almost always the same value.
 */
#include <concepts>
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43499/memory/heap_ops.hpp"

namespace ghostlock::backend::cve_2026_43499 {
    using ghostlock::backend::cve_2026_43499::memory::OpsResult;

    /** What one sampling event is opened with. The fields mirror the retired tree's attr, all
     *  of them inputs: which clock is sampled, what a record should carry, and which register
     *  set to include. The retired path watched the software CPU clock and asked for the
     *  interrupted register set with a thirty-two-bit mask -- which is where the thirty-two
     *  values the voting rule reads come from. */
    struct PerfOpenArgs final {
        /** Which kind of event: software or hardware, as the kernel numbers them. */
        std::uint32_t type = 0U;
        /** Which event of that kind, for example the software CPU clock. */
        std::uint64_t config = 0U;
        /** How many events between samples. */
        std::uint64_t sample_period = 0U;
        /** Which process to watch: zero means this one. */
        std::int32_t pid = 0;
        /** Which processor, or -1 for any. */
        std::int32_t cpu = -1;
        /** The kind of sample wanted, as the kernel numbers them. */
        std::uint32_t sample_type = 0U;
        /** Which registers a sample should include, as a mask. */
        std::uint64_t sample_regs_intr = 0U;
        /** Whether the event should start disabled, which the retired path wanted so that it
         *  could map the ring before anything was written into it. */
        bool start_disabled = false;
        /** Which contexts to leave out: user, hypervisor and idle, as the kernel takes them. */
        bool exclude_user = false;
        bool exclude_hv = false;
        bool exclude_idle = false;
    };

    /** The calls the primitive makes. */
    template <class Seam>
    concept PerfOps = requires(const PerfOpenArgs &args, std::int32_t fd, void *address,
                               std::size_t bytes, std::uint64_t request) {
        { Seam::perf_open(args) } -> std::same_as<OpsResult>;
        { Seam::perf_map(fd, bytes) } -> std::same_as<void *>;
        { Seam::perf_ioctl(fd, request) } -> std::same_as<OpsResult>;
        /* The two the caller makes itself: count something so the sampler has work to do. */
        { Seam::spin_once() } -> std::same_as<OpsResult>;
    };

    /** The request numbers the enable and disable calls use: mechanism, from the perf ABI. */
    inline constexpr std::uint64_t kPerfEventIocEnable = 0U;
    /** The software CPU clock, which the retired path sampled. */
    inline constexpr std::uint64_t kPerfCountSwCpuClock = 0U;
    /** The two sample-kind bits involved: the instruction pointer and the interrupted set. */
    inline constexpr std::uint32_t kPerfSampleIpBit = 1U << 0U;
    /* The wanted sample is the interrupted register set, which the kernel numbers bit 18. Bit 14
     *  is the user register set, and the probe excludes user sampling, so asking for that one made
     *  the kernel omit the block and every record came back as header plus instruction pointer. */
    inline constexpr std::uint32_t kPerfSampleRegsIntrBit = 1U << 18U;
    inline constexpr std::uint64_t kPerfEventIocDisable = 1U;
    /** Where the reader finds the two ends of the ring inside the mapped page. */
    inline constexpr std::size_t kPerfDataHeadOffset = 1024U;
    inline constexpr std::size_t kPerfDataTailOffset = 1032U;
    inline constexpr std::size_t kPerfDataOffsetOffset = 1040U;
    inline constexpr std::size_t kPerfDataSizeOffset = 1048U;
}  // namespace ghostlock::backend::cve_2026_43499
