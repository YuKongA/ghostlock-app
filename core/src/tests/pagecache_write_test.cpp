/** @file tests/pagecache_write_test.cpp
 *  The sixteen-byte write, against a splice face that records instead of sending.
 *
 *  What matters here is order, because the old code had one and the table confirmed it: the vector
 *  is derived from the block being replaced, so a read that fails means no datagram at all; the
 *  sequence is written into the header before the send and advanced only after it succeeded, so a
 *  send that fails leaves the sequence where it was; and closing clears the material the window kept
 *  rather than leaving it in memory for the life of the process.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "backend/cve_2026_43284/seam/pagecache_write.hpp"

namespace {
    using ghostlock::backend::cve_2026_43284::SaMaterial;
    using ghostlock::backend::cve_2026_43284::seam::DoorOps;
    using ghostlock::backend::cve_2026_43284::seam::PageCacheWrite;
    using ghostlock::backend::cve_2026_43284::seam::SpliceOps;
    using ghostlock::backend::cve_2026_43284::seam::WriteOutcome;

    std::size_t g_failures = 0U;

    void expect(bool condition, const char *what) {
        if (!condition) {
            std::printf("FAIL: %s\n", what);
            ++g_failures;
        }
    }

    class FakeDoors final : public DoorOps {
    public:
        [[nodiscard]] int open_target() noexcept override { return 7; }
        [[nodiscard]] int connect_esp(const SaMaterial &) noexcept override { return 8; }
        void close_fd(int) noexcept override {}
    };

    class RecordingSplice final : public SpliceOps {
    public:
        bool read_ok = true;
        bool send_ok = true;
        int iv_calls = 0;
        int sent = 0;
        std::uint32_t sequence_in_header = 0U;
        std::uint32_t spi_in_header = 0U;

        [[nodiscard]] long read_at(int, std::uint8_t *out, std::size_t size,
                                   std::uint64_t) noexcept override {
            if (!read_ok) {
                return -1;
            }
            for (std::size_t i = 0U; i < size; ++i) {
                out[i] = 0x11U;
            }
            return static_cast<long>(size);
        }
        [[nodiscard]] bool splice_one(int, const std::uint8_t *datagram, std::size_t,
                                      std::uint64_t) noexcept override {
            if (!send_ok) {
                return false;
            }
            ++sent;
            spi_in_header = (static_cast<std::uint32_t>(datagram[0]) << 24U) |
                            (static_cast<std::uint32_t>(datagram[1]) << 16U) |
                            (static_cast<std::uint32_t>(datagram[2]) << 8U) |
                            static_cast<std::uint32_t>(datagram[3]);
            sequence_in_header = (static_cast<std::uint32_t>(datagram[4]) << 24U) |
                                 (static_cast<std::uint32_t>(datagram[5]) << 16U) |
                                 (static_cast<std::uint32_t>(datagram[6]) << 8U) |
                                 static_cast<std::uint32_t>(datagram[7]);
            return true;
        }
        void compute_iv(const std::uint8_t *, const std::uint8_t *,
                        std::uint8_t *out) noexcept override {
            ++iv_calls;
            for (std::size_t i = 0U; i < 16U; ++i) {
                out[i] = 0x22U;
            }
        }
        void compute_icv(const std::uint8_t *, std::size_t, const std::uint8_t *,
                         std::uint8_t, std::uint8_t *) noexcept override {}
    };

    SaMaterial frame() noexcept {
        SaMaterial sa;
        sa.spi = 0x0A0B0C0DU;
        sa.encap_port = 4500U;
        sa.sender_port = 4501U;
        sa.icv_len = 12U;
        sa.aes_key[0] = 0xAAU;
        sa.hmac_key[0] = 0xBBU;
        sa.set = true;
        return sa;
    }

    void one_block_is_read_vectored_and_sent() {
        FakeDoors doors;
        RecordingSplice splice;
        PageCacheWrite write(doors, splice);
        expect(write.open_doors(frame()), "the doors open with the frame");
        std::uint8_t block[16] = {};
        block[0] = 0x48U;
        expect(write.write_block(0x40U, block), "one block goes out");
        expect(splice.iv_calls == 1, "derived from the old block that was read first");
        expect(splice.sent == 1, "and exactly one datagram was sent");
        expect(splice.spi_in_header == 0x0A0B0C0DU, "with the spi big-endian in the header");
        expect(splice.sequence_in_header == 1U, "and the first sequence in it");
        expect(write.next_seq() == 2U, "the sequence advanced after the send succeeded");
    }

    void a_failed_read_means_no_datagram() {
        FakeDoors doors;
        RecordingSplice splice;
        splice.read_ok = false;
        PageCacheWrite write(doors, splice);
        static_cast<void>(write.open_doors(frame()));
        std::uint8_t block[16] = {};
        expect(!write.write_block(0x40U, block), "without the old block there is no vector");
        expect(write.outcome() == WriteOutcome::NoOldBlock, "and the reason is named");
        expect(splice.sent == 0 && splice.iv_calls == 0, "so nothing was derived and nothing sent");
    }

    void a_failed_send_leaves_the_sequence_alone() {
        FakeDoors doors;
        RecordingSplice splice;
        splice.send_ok = false;
        PageCacheWrite write(doors, splice);
        static_cast<void>(write.open_doors(frame()));
        std::uint8_t block[16] = {};
        expect(!write.write_block(0x40U, block), "the send fails");
        expect(write.outcome() == WriteOutcome::NotSent, "and says so");
        expect(write.next_seq() == 1U, "and the sequence did not move, which is the old order");
    }
}  // namespace

int main() {
    std::printf("page cache write: one block\n");
    one_block_is_read_vectored_and_sent();
    std::printf("page cache write: no old block\n");
    a_failed_read_means_no_datagram();
    std::printf("page cache write: a failed send\n");
    a_failed_send_leaves_the_sequence_alone();
    if (g_failures != 0U) {
        std::printf("page cache write: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("page cache write: PASS (read, vector, send, then advance)\n");
    return 0;
}
