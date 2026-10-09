/** @file tests/perf_ring_test.cpp
 *  The ring walk over a synthetic ring built by hand, byte by byte.
 *
 *  The records are written the way the kernel writes them: a type word, a size word, and for
 *  a sample an instruction pointer, an ABI tag and a set of registers. Three of them exist to
 *  be refused -- one of another type, one whose ABI tag is neither of the two that carry
 *  registers, and one of size zero, which ends the walk rather than being stepped over.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "backend/cve_2026_43499/perf_ring.hpp"

namespace {
    using ghostlock::backend::cve_2026_43499::DirectMapBounds;
    using ghostlock::backend::cve_2026_43499::kPerfAbiTwo;
    using ghostlock::backend::cve_2026_43499::kPerfRecordSample;
    using ghostlock::backend::cve_2026_43499::RingView;
    using ghostlock::backend::cve_2026_43499::walk_perf_ring;

    constexpr DirectMapBounds kMap{0xffff800000000000ULL, 0xffffc88000000000ULL};
    constexpr std::size_t kRingBytes = 4096U;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    unsigned char g_ring[kRingBytes] = {};

    void put_word32(std::size_t at, std::uint32_t value) {
        for (std::size_t i = 0U; i < 4U; ++i) {
            g_ring[at + i] = static_cast<unsigned char>((value >> (8U * i)) & 0xffU);
        }
    }

    void put_word64(std::size_t at, std::uint64_t value) {
        for (std::size_t i = 0U; i < 8U; ++i) {
            g_ring[at + i] = static_cast<unsigned char>((value >> (8U * i)) & 0xffU);
        }
    }

    /** One sample record, with a full register set the way the kernel writes one: the reader
     *  reads a fixed number of registers and does not bound that by the record size, so a
     *  short record here would let one sample collect the next one values. */
    std::size_t write_sample(std::size_t at, std::uint64_t abi, std::uint64_t first, std::uint64_t second) {
        const std::size_t abi_at = at + 8U + 8U; /* header, then the instruction pointer */
        put_word64(abi_at, abi);
        const std::size_t values_at = abi_at + 8U;
        for (std::size_t i = 0U; i < 32U; ++i) {
            put_word64(values_at + (8U * i), 0x10U); /* not an address, so it is not collected */
        }
        put_word64(values_at, first);
        put_word64(values_at + 8U, second);
        const std::uint32_t size = static_cast<std::uint32_t>((values_at + (8U * 32U)) - at);
        put_word32(at, kPerfRecordSample);
        put_word32(at + 4U, size);
        return size;
    }

    void a_walk_collects_from_the_samples_it_should() {
        std::memset(g_ring, 0, sizeof(g_ring));
        std::size_t at = 0U;
        at += write_sample(at, 1U, 0xf0ff800000100000ULL, 0xf0ff800000100000ULL);
        /* Another type: not a sample, so nothing is read from it. */
        put_word32(at, 1U);
        put_word32(at + 4U, 16U);
        at += 16U;
        at += write_sample(at, 3U, 0xf0ff800000300000ULL, 0xf0ff800000300000ULL); /* wrong abi */
        at += write_sample(at, kPerfAbiTwo, 0xf0ff800000100000ULL, 0xf0ff800000200000ULL);
        std::uint64_t out[64] = {};
        RingView ring;
        ring.buffer = g_ring;
        ring.data_size = kRingBytes;
        ring.tail = 0U;
        ring.head = at;
        const auto outcome = walk_perf_ring(ring, kMap, out, 64U);
        expect(outcome.samples == 2U, "only the two samples with a register ABIs tag are read");
        expect(outcome.kept == 4U, "two in-map addresses from each of the two samples that qualify");
        expect(out[0] == 0xffff800000100000ULL && out[1] == 0xffff800000100000ULL,
               "the first sample contributes both of its in-map values, tags restored");
        expect(out[3] == 0xffff800000200000ULL, "and the second its own");
        expect(outcome.position == at, "the walk ends where the records ended");
        expect(!outcome.stopped_early, "and did not stop early");
    }

    void a_zero_sized_record_ends_the_walk() {
        std::memset(g_ring, 0, sizeof(g_ring));
        std::size_t at = 0U;
        at += write_sample(at, 1U, 0xf0ff800000100000ULL, 0xf0ff800000100000ULL);
        const std::size_t before_zero = at;
        put_word32(at, kPerfRecordSample);
        put_word32(at + 4U, 0U); /* the writer has not finished this one */
        at += 24U;
        at += write_sample(at, 1U, 0xf0ff800000400000ULL, 0xf0ff800000400000ULL);
        std::uint64_t out[64] = {};
        RingView ring;
        ring.buffer = g_ring;
        ring.data_size = kRingBytes;
        ring.tail = 0U;
        ring.head = at;
        const auto outcome = walk_perf_ring(ring, kMap, out, 64U);
        expect(outcome.stopped_early, "a record of size zero ends the walk");
        expect(outcome.position == before_zero, "at the record that could not be read");
        expect(outcome.kept == 2U, "so what came after it is not read, whatever it holds");
        RingView nothing;
        nothing.buffer = nullptr;
        expect(walk_perf_ring(nothing, kMap, out, 64U).kept == 0U, "and no buffer reads nothing");
    }
}  // namespace

int main() {
    std::printf("perf ring: the samples it reads\n");
    a_walk_collects_from_the_samples_it_should();
    std::printf("perf ring: the record that ends it\n");
    a_zero_sized_record_ends_the_walk();
    if (g_failures != 0U) {
        std::printf("perf ring: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("perf ring: PASS (reads the samples it should, stops where it must)\n");
    return 0;
}
