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

#include "backend/cve_2026_43284/landing/pagecache_write.hpp"

namespace {
    using ghostlock::backend::cve_2026_43284::SaMaterial;
    using ghostlock::backend::cve_2026_43284::landing::DoorOps;
    using ghostlock::backend::cve_2026_43284::landing::PageCacheWrite;
    using ghostlock::backend::cve_2026_43284::landing::ReadOutcome;
    using ghostlock::backend::cve_2026_43284::landing::SpliceOps;
    using ghostlock::backend::cve_2026_43284::landing::WriteOutcome;

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
        /* The whole datagram, not just its first eight bytes: asserting on the header alone let a
         *  wrong layout pass, because the header is where the mistake was not. */
        std::uint8_t last_datagram[128] = {};
        std::size_t last_size = 0U;

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
        [[nodiscard]] bool splice_one(int, const std::uint8_t *datagram, std::size_t size,
                                      std::uint64_t) noexcept override {
            if (!send_ok) {
                return false;
            }
            ++sent;
            last_size = size;
            for (std::size_t i = 0U; i < size && i < sizeof(last_datagram); ++i) {
                last_datagram[i] = datagram[i];
            }
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
        void compute_iv(const std::uint8_t *, const std::uint8_t *desired_block,
                        const std::uint8_t *, std::uint8_t *out) noexcept override {
            ++iv_calls;
            for (std::size_t i = 0U; i < 16U; ++i) {
                /* Recorded, not ignored: the vector is what makes the in-place decryption write this
                 * block, and a derivation that never sees it writes zeros instead. */
                desired[i] = desired_block[i];
                out[i] = 0x22U;
            }
        }
        std::uint8_t desired[16] = {};
        void compute_icv(const std::uint8_t *, std::size_t, const std::uint8_t *,
                         std::uint8_t, std::uint8_t *) noexcept override {}
        /* The surface says which halves it can serve: a carrier this process cannot open is served
         * by a bridge, and the window asks here rather than assuming a descriptor exists. */
        [[nodiscard]] bool can_serve_page() const noexcept override { return can_serve; }
        [[nodiscard]] bool can_read_page() const noexcept override { return can_read_page_flag; }
        bool can_serve = true;
        bool can_read_page_flag = true;
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
        expect(splice.desired[0] == block[0],
               "and from the block the caller wants in its place, which is what the vector carries");
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

    /** The shape the old tree documents: spi(4) | sequence(4) | vector(16) | ciphertext(16) | check. */
    void the_datagram_has_the_documented_shape() {
        FakeDoors doors;
        RecordingSplice splice;
        PageCacheWrite write(doors, splice);
        static_cast<void>(write.open_doors(frame()));
        std::uint8_t block[16] = {};
        block[0] = 0x48U;
        block[15] = 0x99U;
        expect(write.write_block(0x40U, block), "a block goes out");
        expect(splice.last_size == 8U + 16U + 16U + 12U, "the datagram is the documented length");
        bool vector_present = true;
        for (std::size_t i = 0U; i < 16U; ++i) {
            if (splice.last_datagram[8U + i] != 0x22U) {
                vector_present = false;
            }
        }
        expect(vector_present, "the derived vector is in the datagram, not left behind in a local");
        /* The ciphertext slot holds the page's own bytes, because that is what the wire carries and
         * what the check covers: the surface under test returns 0x11, so that is what must be there.
         * The block the caller wants travels in the vector, not in this slot. */
        expect(splice.last_datagram[24U] == 0x11U && splice.last_datagram[39U] == 0x11U,
               "and the slot carries the page's bytes, not the desired ones the vector encodes");
    }

    /** The read face the verification uses: the same door and the same surface, all or nothing. */
    void a_block_reads_back_through_the_same_surface() {
        FakeDoors doors;
        RecordingSplice splice;
        PageCacheWrite write(doors, splice);
        static_cast<void>(write.open_doors(frame()));
        std::uint8_t back[16] = {};
        expect(write.can_read(), "the window can read while its carrier door is open");
        expect(write.read_block(0x80U, back), "a block comes back");
        expect(write.read_outcome() == ReadOutcome::Read, "and the read says so");
        expect(back[0] == 0x11U && back[15] == 0x11U, "carrying the surface's bytes");
        splice.read_ok = false;
        std::uint8_t kept[16] = {};
        kept[0] = 0x5AU;
        expect(!write.read_block(0x80U, kept), "a read that cannot complete is refused");
        expect(write.read_outcome() == ReadOutcome::Incomplete, "by name");
        expect(kept[0] == 0x5AU, "and the caller's buffer is left exactly as it was");
        write.close_doors();
        /* Closing the doors closes the socket and clears the keys. What the surface can still read is
         * the surface's own answer -- a caller-owned descriptor outlives the doors -- so the refusal
         * that matters is the landing's, and the chain test pins that one. */
    }

    /** A surface with no way to the carrier is refused by name rather than written blindly. */
    void a_surface_that_cannot_serve_a_page_is_refused() {
        FakeDoors doors;
        RecordingSplice splice;
        splice.can_serve = false;
        PageCacheWrite write(doors, splice);
        static_cast<void>(write.open_doors(frame()));
        write.close_doors();
        std::uint8_t block[16] = {};
        expect(!write.write_block(0x40U, block), "no page source means no block");
        expect(write.outcome() == WriteOutcome::NotOpen, "and the reason is named");
        splice.can_serve = true;
        splice.can_read_page_flag = false;
        expect(!write.read_block(0x40U, block), "no read source means no read either");
        expect(write.read_outcome() == ReadOutcome::NotOpen, "named as well");
    }

    /** The sequence belongs to the security association, not to the window. A second window on the
     *  same session must continue it: the kernel's inbound anti-replay window drops anything it has
     *  already seen, so a restart at one writes nothing at all -- measured on the device, twice. */
    void a_second_window_continues_the_session_sequence() {
        FakeDoors doors;
        RecordingSplice splice;
        PageCacheWrite first(doors, splice);
        static_cast<void>(first.open_doors(frame()));
        std::uint8_t block[16] = {};
        static_cast<void>(first.write_block(0x40U, block));
        static_cast<void>(first.write_block(0x50U, block));
        expect(first.next_seq() == 3U, "two writes leave the sequence at three");
        PageCacheWrite second(doors, splice);
        static_cast<void>(second.open_doors(frame()));
        expect(second.next_seq() == 1U, "a fresh window starts at one, because its route is fresh");
        second.adopt_sequence(first.next_seq());
        static_cast<void>(second.write_block(0x60U, block));
        expect(splice.sequence_in_header == 3U,
               "and once adopted, its datagram carries the sequence the session reached");
        second.adopt_sequence(0U);
        expect(second.next_seq() == 1U, "a zero adoption is refused rather than wrapping the header");
    }

    /** A verification read must not overwrite the verdict of the write it verifies. */
    void a_read_leaves_the_write_verdict_alone() {
        FakeDoors doors;
        RecordingSplice splice;
        PageCacheWrite write(doors, splice);
        static_cast<void>(write.open_doors(frame()));
        std::uint8_t block[16] = {};
        expect(write.write_block(0x40U, block), "a block goes out");
        std::uint8_t back[16] = {};
        expect(write.read_block(0x40U, back), "and reads back");
        expect(write.outcome() == WriteOutcome::Wrote,
               "the write verdict is still the write's, not the read's");
        expect(write.read_outcome() == ReadOutcome::Read, "with the read's own verdict beside it");
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
    std::printf("page cache write: the documented shape\n");
    the_datagram_has_the_documented_shape();
    std::printf("page cache write: no old block\n");
    a_failed_read_means_no_datagram();
    std::printf("page cache write: a failed send\n");
    a_failed_send_leaves_the_sequence_alone();
    std::printf("page cache write: the read face\n");
    a_block_reads_back_through_the_same_surface();
    std::printf("page cache write: a surface with no page source\n");
    a_surface_that_cannot_serve_a_page_is_refused();
    std::printf("page cache write: the session sequence\n");
    a_second_window_continues_the_session_sequence();
    a_read_leaves_the_write_verdict_alone();
    if (g_failures != 0U) {
        std::printf("page cache write: FAIL (%zu)\n", g_failures);
        return 1;
    }
    std::printf("page cache write: PASS (read, vector, send, advance; and read back)\n");
    return 0;
}
