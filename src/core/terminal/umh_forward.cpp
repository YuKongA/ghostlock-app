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
#include <cerrno>
#include <cstdio>
#include <unistd.h>
#endif

namespace ghostlock::terminal {
    namespace {
#if defined(__linux__)
        /* The UMH script's success marker. It is the one criterion the chain's
         * terminus and a privilege-poor client can both observe: an app-domain
         * process may stat this file even though its policy denies listing
         * /data/local/tmp and denies reading /proc/modules and the /dev/dfm0
         * node. */
        constexpr const char *kLkmOkMarker = "/data/local/tmp/.ghostlock_lkm_ok";
        /* Upstream-style marker pair, used as the fallback observation. */
        constexpr const char *kLkmLegacyMarker = "/dev/dfm0";

        enum class MarkerState : std::uint8_t { Present, Absent, Unobservable };

        /* access(2) with errno classification: a policy denial (EACCES/EPERM) is
         * "this domain cannot observe the marker", which is not evidence that
         * the terminus failed; only ENOENT is. */
        MarkerState marker_state(const char *path) noexcept {
            errno = 0;
            if (::access(path, F_OK) == 0) {
                return MarkerState::Present;
            }
            return (errno == EACCES || errno == EPERM) ? MarkerState::Unobservable
                                                       : MarkerState::Absent;
        }

        /* Read-only confirmation that the LKM/UMH terminus completed. Markers are
         * checked from the least privileged domain outward. Nothing here writes,
         * forwards or execs. */
        UmhReadyState probe_kernel_umh(void *) noexcept {
            if (marker_state(kLkmOkMarker) == MarkerState::Present) {
                return UmhReadyState::Ready;
            }
            const MarkerState legacy = marker_state(kLkmLegacyMarker);
            if (legacy == MarkerState::Present) {
                std::FILE *modules = std::fopen("/proc/modules", "re");
                if (modules == nullptr) {
                    const int open_errno = errno;
                    return (open_errno == EACCES || open_errno == EPERM)
                                   ? UmhReadyState::Unavailable
                                   : UmhReadyState::NotReady;
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
            /* Nothing observed: denied by policy (cannot see) is Unavailable;
             * genuinely absent (neither marker exists) is NotReady. */
            return legacy == MarkerState::Unobservable ? UmhReadyState::Unavailable
                                                       : UmhReadyState::NotReady;
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
        const UmhReadyState ready = input.channel.ready(input.channel.ctx);
        if (ready == UmhReadyState::Unavailable) {
            /* Unobservable is not negative: the backend already proved the LKM
             * terminus from the chain, and a domain whose policy hides the
             * markers (/proc/modules and the /dev node are denied to apps) must
             * not fail an otherwise complete run. Recorded as degraded. */
#if defined(__linux__)
            (void)std::fputs("umh_forward degraded=probe_unobservable\n", stderr);
#endif
            return StageResult::Done;
        }
        if (ready != UmhReadyState::Ready) {
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
