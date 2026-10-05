/* umh_forward terminal execution policy (B5-8 / B6-T5) -- implementation.
 *
 * Read-only and host-testable: run_umh_forward touches no device, forks
 * nothing and writes no file. The readiness probe is the injected
 * UmhForwardChannel handle; production_umh_channel() binds the fixed
 * /dev/dfm0 + /proc/modules read-only check under __linux__. */

#include "terminal/umh_forward.hpp"

#include <cstddef>
#include <string_view>

#if defined(__linux__)
#include <cstdio>
#include <unistd.h>
#endif

namespace ghostlock::terminal {
    namespace {
#if defined(__linux__)
        /* Read-only confirmation: /dev/dfm0 exists (the LKM's success marker)
         * and /proc/modules lists a module whose name contains "kernelsu".
         * Neither check writes, forwards or execs anything. */
        UmhReadyState probe_kernel_umh(void *) noexcept {
            if (::access("/dev/dfm0", F_OK) != 0) {
                return UmhReadyState::NotReady;
            }
            std::FILE *modules = std::fopen("/proc/modules", "re");
            if (modules == nullptr) {
                return UmhReadyState::Unavailable;
            }
            bool loaded = false;
            char line[512];
            while (std::fgets(line, sizeof(line), modules) != nullptr) {
                if (std::string_view(line).find("kernelsu") !=
                    std::string_view::npos) {
                    loaded = true;
                    break;
                }
            }
            (void)std::fclose(modules);
            return loaded ? UmhReadyState::Ready : UmhReadyState::NotReady;
        }
#endif
    } // namespace

    StageResult run_umh_forward(UmhForwardInput &input) noexcept {
        /* The backend fills the input only after a clean LKM/UMH terminus. The
         * chain has already completed the load; this terminal only confirms the
         * read-only readiness markers, so it never writes/forwards and never
         * needs the UMH command or the session secrets. */
        if (!input.lkm_loaded) {
            return StageResult::Failed;
        }
        if (!input.channel.valid()) {
            return StageResult::Failed;
        }
        if (input.channel.ready(input.channel.ctx) != UmhReadyState::Ready) {
            return StageResult::Failed;
        }
        return StageResult::Done;
    }

    UmhForwardChannel production_umh_channel() noexcept {
#if defined(__linux__)
        UmhForwardChannel channel{};
        channel.ctx = nullptr;
        channel.ready = &probe_kernel_umh;
        return channel;
#else
        /* A non-Linux host has no LKM/UMH terminus; leave the channel invalid
         * so the terminal fails closed instead of assuming success. */
        return UmhForwardChannel{};
#endif
    }

    StageResult UmhForwardPolicy::run(CoreSession &session, Input &input) {
        (void)session;
        return run_umh_forward(input);
    }

} // namespace ghostlock::terminal
