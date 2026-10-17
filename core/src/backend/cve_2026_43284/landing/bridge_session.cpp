/** @file backend/cve_2026_43284/landing/bridge_session.cpp
 *  The parent side of the session: one fork, then one request and one reply per block.
 *
 *  Everything here is about endings rather than beginnings, because the beginning is one fork: a
 *  request is written inside a deadline and answered inside the same deadline, and any answer that is
 *  not the one the op asked for ends the session for good -- the block fails, and so does every block
 *  after it. The design says fail closed; this file is where that is enforced.
 */
#include "backend/cve_2026_43284/landing/bridge_session.hpp"

#include "jni/gl_log.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <ctime>

#include <poll.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

namespace ghostlock::backend::cve_2026_43284::landing {
    namespace {
        /** How long the child is given to leave after its request socket is closed. Same 200 ms: a
         *  helper that ignores its stdin must not hold the write open. */
        constexpr int kReapGraceMs = 200;
        /** The reap grace in ten-millisecond steps. */
        constexpr int kReapSteps = kReapGraceMs / 10;
        constexpr long kReapStepNanos = 10L * 1000L * 1000L;

        /** Monotonic microseconds, or zero when the clock cannot be read (the caller then measures a
         *  request as instant, which only affects the log's number). */
        [[nodiscard]] std::int64_t now_us() noexcept {
            timespec stamp{};
            if (::clock_gettime(CLOCK_MONOTONIC, &stamp) != 0) {
                return 0;
            }
            return static_cast<std::int64_t>(stamp.tv_sec) * 1000000 +
                   static_cast<std::int64_t>(stamp.tv_nsec) / 1000;
        }

        /** How much of the deadline is left, in the requests' own unit. */
        [[nodiscard]] int remaining_ms(std::int64_t deadline_us) noexcept {
            const std::int64_t left = deadline_us - now_us();
            if (left <= 0) {
                return 0;
            }
            const std::int64_t millis = left / 1000;
            return millis >= static_cast<std::int64_t>(kBridgeTimeoutMs)
                       ? kBridgeTimeoutMs
                       : static_cast<int>(millis);
        }

        /** Waits for one descriptor to carry the event asked for, or gives up at the deadline.
         *  A hang-up counts as ready: the caller's own read then sees end of file, which is the
         *  honest answer, rather than this helper inventing one. */
        [[nodiscard]] bool wait_ready(int fd, short events, std::int64_t deadline_us) noexcept {
            if (fd < 0) {
                return false;
            }
            struct pollfd entry {};
            entry.fd = fd;
            entry.events = events;
            for (;;) {
                const int ready = ::poll(&entry, 1, remaining_ms(deadline_us));
                if (ready > 0) {
                    return entry.revents != 0;
                }
                if (ready == 0) {
                    return false;
                }
                if (errno != EINTR) {
                    return false;
                }
            }
        }

        void store_le64(std::uint8_t *out, std::uint64_t value) noexcept {
            for (std::size_t i = 0U; i < 8U; ++i) {
                out[i] = static_cast<std::uint8_t>((value >> (8U * i)) & 0xffULL);
            }
        }

        [[nodiscard]] std::uint32_t load_le32(const std::uint8_t *in) noexcept {
            std::uint32_t value = 0U;
            for (std::size_t i = 0U; i < 4U; ++i) {
                value |= static_cast<std::uint32_t>(in[i]) << (8U * i);
            }
            return value;
        }

        [[nodiscard]] std::uint64_t load_le64(const std::uint8_t *in) noexcept {
            std::uint64_t value = 0ULL;
            for (std::size_t i = 0U; i < 8U; ++i) {
                value |= static_cast<std::uint64_t>(in[i]) << (8U * i);
            }
            return value;
        }

        /** A dead peer must not raise SIGPIPE: the write has to report the failure, not die of it. */
        constexpr int kSendFlags =
#if defined(MSG_NOSIGNAL)
            MSG_NOSIGNAL;
#else
            0;
#endif

        /** Copy a path in, or refuse: these strings become argv, and a truncated argv is a different
         *  command -- the same rule the one-shot bridge keeps, spelled the same way. */
        [[nodiscard]] bool take_path(char *out, std::size_t capacity, const char *path,
                                     std::size_t length) noexcept {
            if (path == nullptr || length == 0U || length + 1U > capacity) {
                return false;
            }
            for (std::size_t i = 0U; i < length; ++i) {
                const char c = path[i];
                if (c == '\0' || c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                    return false;
                }
            }
            std::memcpy(out, path, length);
            out[length] = '\0';
            return true;
        }
    }  // namespace

    BridgeSession::~BridgeSession() noexcept {
        close();
    }

    bool BridgeSession::bind(const char *helper_path, std::size_t helper_bytes,
                             const char *carrier_path, std::size_t carrier_bytes) noexcept {
        bound_ = take_path(helper_, sizeof(helper_), helper_path, helper_bytes) &&
                 take_path(carrier_, sizeof(carrier_), carrier_path, carrier_bytes);
        if (!bound_) {
            outcome_ = Outcome::NotBound;
        }
        return bound_;
    }

    bool BridgeSession::open() noexcept {
        if (open_) {
            return true;
        }
        if (!bound_) {
            outcome_ = Outcome::NotBound;
            return false;
        }
        if (dead_) {
            /* A session that failed once stays failed: the design's fail-closed rule, and the reason
             *  a caller does not have to remember which block broke. */
            return false;
        }
        int request_pair[2] = {-1, -1};
        if (::socketpair(AF_UNIX, SOCK_DGRAM, 0, request_pair) != 0) {
            outcome_ = Outcome::PipeFailed;
            GL_LOGI("bridge: session refused socketpair errno=%d", errno);
            return false;
        }
        int reply_pair[2] = {-1, -1};
        if (::pipe(reply_pair) != 0) {
            outcome_ = Outcome::PipeFailed;
            (void) ::close(request_pair[0]);
            (void) ::close(request_pair[1]);
            GL_LOGI("bridge: session refused pipe errno=%d", errno);
            return false;
        }
#if defined(SO_NOSIGPIPE)
        {
            const int on = 1;
            (void) ::setsockopt(request_pair[0], SOL_SOCKET, SO_NOSIGPIPE, &on,
                                static_cast<socklen_t>(sizeof(on)));
        }
#endif
        const pid_t child = ::fork();
        if (child < 0) {
            outcome_ = Outcome::SpawnFailed;
            (void) ::close(request_pair[0]);
            (void) ::close(request_pair[1]);
            (void) ::close(reply_pair[0]);
            (void) ::close(reply_pair[1]);
            GL_LOGI("bridge: session refused fork errno=%d", errno);
            return false;
        }
        if (child == 0) {
            /* In the child: the request socket becomes standard input and the reply pipe standard
             *  output, so the helper reads requests where it used to read nothing and answers where it
             *  used to write one block. The same exec the one-shot bridge runs, plus the mode word. */
            (void) ::close(request_pair[0]);
            (void) ::close(reply_pair[0]);
            if (::dup2(request_pair[1], STDIN_FILENO) < 0 ||
                ::dup2(reply_pair[1], STDOUT_FILENO) < 0) {
                ::_exit(127);
            }
            if (request_pair[1] != STDIN_FILENO) {
                (void) ::close(request_pair[1]);
            }
            if (reply_pair[1] != STDOUT_FILENO) {
                (void) ::close(reply_pair[1]);
            }
            ::execl(helper_, "crashdump64", "0", carrier_, "session",
                    static_cast<char *>(nullptr));
            ::_exit(127);
        }
        (void) ::close(request_pair[1]);
        (void) ::close(reply_pair[1]);
        request_fd_ = request_pair[0];
        reply_fd_ = reply_pair[0];
        child_ = child;
        open_ = true;
        GL_LOGI("bridge: opened pid=%d mode=session", static_cast<int>(child));
        if (!handshake()) {
            GL_LOGI("bridge: refused session pid=%d outcome=%d (a helper that is not a session helper must not be believed)",
                    static_cast<int>(child), static_cast<int>(outcome_));
            dead_ = true;
            close();
            return false;
        }
        outcome_ = Outcome::Ok;
        GL_LOGI("bridge: session ready pid=%d helper=%s carrier=%s",
                static_cast<int>(child), helper_, carrier_);
        return true;
    }

    long BridgeSession::read16(std::uint64_t offset, std::uint8_t out[kBlockBytes]) noexcept {
        if (out == nullptr) {
            return -1;
        }
        return request(Op::Read, offset, -1, out);
    }

    long BridgeSession::splice16_into_pipe(std::uint64_t offset, int pipe_write_fd) noexcept {
        if (pipe_write_fd < 0) {
            return -1;
        }
        return request(Op::Splice, offset, pipe_write_fd, nullptr);
    }

    void BridgeSession::close() noexcept {
        if (request_fd_ < 0 && reply_fd_ < 0 && child_ <= 0) {
            return; /* Idempotent: nothing is open, so nothing is closed and nothing is logged twice. */
        }
        const pid_t pid = child_;
        const int closed = close_channels();
        open_ = false;
        int wait = 0;
        if (child_ > 0) {
            int status = 0;
            bool reaped = false;
            for (int step = 0; step < kReapSteps; ++step) {
                const pid_t got = ::waitpid(child_, &status, WNOHANG);
                if (got == child_) {
                    reaped = true;
                    break;
                }
                if (got < 0 && errno != EINTR) {
                    break;
                }
                const timespec pause{0, kReapStepNanos};
                (void) ::nanosleep(&pause, nullptr);
            }
            if (!reaped) {
                /* The channels are gone, so a helper that is still alive is not going to hear anything
                 *  else. It is killed rather than left behind: a stale session child would hold the
                 *  patched image open for the rest of the run. */
                (void) ::kill(child_, SIGKILL);
                for (;;) {
                    const pid_t got = ::waitpid(child_, &status, 0);
                    if (got == child_) {
                        reaped = true;
                        break;
                    }
                    if (got < 0 && errno != EINTR) {
                        break;
                    }
                }
            }
            wait = reaped ? 1 : 0;
            last_status_ = reaped ? status : -1;
            child_ = -1;
        }
        GL_LOGI("bridge: closed pid=%d wait=%d fds=%d", static_cast<int>(pid), wait, closed);
    }

    int BridgeSession::close_channels() noexcept {
        int closed = 0;
        if (request_fd_ >= 0) {
            (void) ::close(request_fd_);
            request_fd_ = -1;
            ++closed;
        }
        if (reply_fd_ >= 0) {
            (void) ::close(reply_fd_);
            reply_fd_ = -1;
            ++closed;
        }
        return closed;
    }

    bool BridgeSession::handshake() noexcept {
        std::uint8_t reply[kBridgeReplyBytes] = {};
        /* The op's own validation lives in request(): the Hello reply has to carry this session's
         *  magic back, which is what a one-shot helper cannot do. */
        return request(Op::Hello, 0U, -1, reply) == static_cast<long>(kBridgeReplyBytes);
    }

    bool BridgeSession::send_request(Op op, std::uint64_t offset, int fd_to_pass,
                                     std::int64_t deadline_us) noexcept {
        std::uint8_t message[kBridgeRequestBytes] = {};
        message[0] = static_cast<std::uint8_t>(op);
        store_le64(message + 8, offset);
        /* Path id zero: the one carrier this session was started for. The field exists so a future
         *  helper can be handed more than one path without another channel. */
        store_le64(message + 16, 0ULL);
        struct iovec body {};
        body.iov_base = message;
        body.iov_len = kBridgeRequestBytes;
        char control[CMSG_SPACE(sizeof(int))] = {};
        struct msghdr message_headers {};
        message_headers.msg_iov = &body;
        message_headers.msg_iovlen = 1U;
        if (fd_to_pass >= 0) {
            /* The splice's destination travels with the request: the parent owns that pipe, and this
             *  is the one way to hand the child a descriptor it did not inherit at exec. */
            message_headers.msg_control = control;
            message_headers.msg_controllen = static_cast<socklen_t>(CMSG_SPACE(sizeof(int)));
            struct cmsghdr *header = CMSG_FIRSTHDR(&message_headers);
            header->cmsg_level = SOL_SOCKET;
            header->cmsg_type = SCM_RIGHTS;
            header->cmsg_len = static_cast<socklen_t>(CMSG_LEN(sizeof(int)));
            std::memcpy(CMSG_DATA(header), &fd_to_pass, sizeof(int));
            message_headers.msg_controllen = static_cast<socklen_t>(CMSG_SPACE(sizeof(int)));
        }
        if (!wait_ready(request_fd_, POLLOUT, deadline_us)) {
            outcome_ = Outcome::TimedOut;
            return false;
        }
        for (;;) {
            const ssize_t sent = ::sendmsg(request_fd_, &message_headers, kSendFlags);
            if (sent < 0) {
                if (errno == EINTR) {
                    continue;
                }
                outcome_ = (errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS)
                               ? Outcome::TimedOut
                               : Outcome::ChildGone;
                return false;
            }
            /* A datagram is all or nothing: a partial one is a broken channel, not a retry. */
            outcome_ = sent == static_cast<ssize_t>(kBridgeRequestBytes) ? Outcome::Ok
                                                                       : Outcome::ChildGone;
            return sent == static_cast<ssize_t>(kBridgeRequestBytes);
        }
    }

    bool BridgeSession::read_reply(std::uint8_t out[kBridgeReplyBytes],
                                   std::int64_t deadline_us) noexcept {
        std::size_t got = 0U;
        while (got < kBridgeReplyBytes) {
            if (!wait_ready(reply_fd_, POLLIN, deadline_us)) {
                outcome_ = got == 0U ? Outcome::TimedOut : Outcome::ShortReply;
                return false;
            }
            const ssize_t read_bytes = ::read(reply_fd_, out + got, kBridgeReplyBytes - got);
            if (read_bytes < 0) {
                if (errno == EINTR) {
                    continue;
                }
                outcome_ = Outcome::ChildGone;
                return false;
            }
            if (read_bytes == 0) {
                /* End of file: the child is gone. Bytes already read are half a reply, which is not
                 *  a reply. */
                outcome_ = got == 0U ? Outcome::ChildGone : Outcome::ShortReply;
                return false;
            }
            got += static_cast<std::size_t>(read_bytes);
        }
        return true;
    }

    long BridgeSession::request(Op op, std::uint64_t offset, int fd_to_pass,
                                std::uint8_t *out) noexcept {
        if (!open_ || request_fd_ < 0 || reply_fd_ < 0) {
            if (!dead_ && outcome_ != Outcome::Ok) {
                outcome_ = Outcome::Closed;
            }
            return -1;
        }
        if (dead_) {
            return -1;
        }
        const std::int64_t deadline = now_us() + static_cast<std::int64_t>(kBridgeTimeoutMs) * 1000;
        const std::int64_t started = now_us();
        ++requests_;
        bool ok = false;
        std::uint8_t reply[kBridgeReplyBytes] = {};
        if (send_request(op, offset, fd_to_pass, deadline) && read_reply(reply, deadline)) {
            if (op == Op::Read) {
                /* A read's reply is the block itself; there is no status to read, because the block
                 *  is what the reply is. */
                if (out != nullptr) {
                    std::memcpy(out, reply, kBridgeReplyBytes);
                }
                ok = true;
            } else {
                const std::uint32_t status = load_le32(reply);
                const std::uint8_t echoed_op = reply[4];
                const std::uint64_t echo = load_le64(reply + 8);
                const std::uint64_t want = op == Op::Hello ? kBridgeSessionMagic : offset;
                ok = status == 0U && echoed_op == static_cast<std::uint8_t>(op) && echo == want;
                if (!ok) {
                    outcome_ = Outcome::BadReply;
                }
            }
        }
        const std::int64_t took = now_us() - started;
        if (!ok) {
            ++failed_;
            dead_ = true;
            if (outcome_ == Outcome::Ok || outcome_ == Outcome::Never) {
                outcome_ = Outcome::BadReply;
            }
        }
        const char letter = op == Op::Read ? 'r' : (op == Op::Splice ? 's' : 'h');
        if (!ok || (requests_ % kBridgeLogEveryRequests) == 0U) {
            GL_LOGI("bridge: request op=%c off=0x%llx us=%lld", letter,
                    static_cast<unsigned long long>(offset), static_cast<long long>(took));
        }
        if (!ok) {
            GL_LOGI("bridge: request failed op=%c off=0x%llx outcome=%d requests=%zu failed=%zu", letter,
                    static_cast<unsigned long long>(offset), static_cast<int>(outcome_), requests_,
                    failed_);
            return -1;
        }
        return static_cast<long>(kBridgeReplyBytes);
    }
}  // namespace ghostlock::backend::cve_2026_43284::landing
