#ifndef GHOSTLOCK_BACKEND_CVE_2026_43499_SCHEMA_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43499_SCHEMA_HPP

/* Owner schema for the cve_2026_43499 profile transport (ADR-0003 / A2-3c-2).
 *
 * kFields is the single declaration of every wire (section, key) the 43499
 * owner reads. It reproduces binary.cpp's former kSections key names, widths,
 * signedness and destinations verbatim, so a bind of a well-formed document
 * yields exactly the same kernel_offsets the legacy field-table walk produced.
 *
 * backend.cve_2026_43499.steps is a backend-private key: it selects the step
 * set (ADR-0004 R18), not a kernel offset, so it binds to View::steps instead
 * of a kernel_offsets member. Declaring it here is what lets a strict
 * (Production) bind accept a real document rather than rejecting the private
 * section as unknown.
 *
 * A2-3c keeps kernel_offsets/TargetProfile physically frozen, so this View
 * wraps the transport instead of replacing it; materialising the View onto the
 * transport is a plain member copy in binary.cpp. The serializer there still
 * uses its legacy Field table; owner_schema_test's full round trip keeps the
 * two declarations in lockstep. */

#include "profile/model.h"
#include "profile/schema.hpp"

#include <cstdint>
#include <type_traits>

namespace ghostlock::backend {
    /* 43499 View: the frozen transport plus the backend-private step-set id. */
    struct Cve2026_43499View {
        profile::kernel_offsets values;
        uint16_t steps = 0;
    };

    namespace schema_detail {
        template<typename T>
        [[nodiscard]] constexpr T from_raw(uint64_t raw) noexcept {
            if constexpr (std::is_signed_v<T>) {
                return static_cast<T>(static_cast<int64_t>(raw));
            } else {
                return static_cast<T>(raw);
            }
        }
    } // namespace schema_detail

    using Cve2026_43499Field = profile::FieldSpec<Cve2026_43499View>;

#define GLK_43499_PLAIN(section, key, member, width) \
    { \
        section, key, width, false, false, \
                [](Cve2026_43499View &view, uint64_t raw) { \
                    view.values.member = \
                            schema_detail::from_raw< \
                                    decltype(view.values.member)>(raw); \
                } \
    }

/* Optional field: absence is meaningful, so the whole optional is assigned. */
#define GLK_43499_OPT(section, key, member, width, sign) \
    { \
        section, key, width, sign, false, \
                [](Cve2026_43499View &view, uint64_t raw) { \
                    view.values.member = schema_detail::from_raw< \
                            std::remove_reference_t< \
                                    decltype(*view.values.member)>>(raw); \
                } \
    }

/* Backend-private StepSet id; never touches kernel_offsets. */
#define GLK_43499_STEPS(section, key) \
    { \
        section, key, 2, false, false, \
                [](Cve2026_43499View &view, uint64_t raw) { \
                    view.steps = static_cast<uint16_t>(raw); \
                } \
    }

    struct Cve2026_43499Schema {
        using View = Cve2026_43499View;

        static constexpr Cve2026_43499Field kFields[] = {
            GLK_43499_PLAIN("meta", "kernel_major", meta.kernel_major, 1),
            GLK_43499_PLAIN("meta", "fallback_route", meta.fallback_route, 1),
            GLK_43499_PLAIN("meta", "safe_mode", meta.safe_mode, 1),
            GLK_43499_PLAIN("meta", "vr_guard", misc.vr_guard, 1),
            GLK_43499_PLAIN("task_struct", "prio", task.prio, 4),
            GLK_43499_PLAIN("task_struct", "normal_prio", task.normal_prio, 4),
            GLK_43499_PLAIN("task_struct", "sched_task_group", task.sched_task_group, 4),
            GLK_43499_PLAIN("task_struct", "pi_lock", task.pi_lock, 4),
            GLK_43499_PLAIN("task_struct", "pi_waiters", task.pi_waiters, 4),
            GLK_43499_PLAIN("task_struct", "pi_top_task", task.pi_top_task, 4),
            GLK_43499_PLAIN("task_struct", "pi_blocked_on", task.pi_blocked_on, 4),
            GLK_43499_PLAIN("task_struct", "pid", task.pid, 4),
            GLK_43499_PLAIN("task_struct", "tgid", task.tgid, 4),
            GLK_43499_PLAIN("task_struct", "atomic_flags", task.atomic_flags, 4),
            GLK_43499_PLAIN("task_struct", "real_cred", task.real_cred, 4),
            GLK_43499_PLAIN("task_struct", "cred", task.cred, 4),
            GLK_43499_PLAIN("task_struct", "comm", task.comm, 4),
            GLK_43499_PLAIN("task_struct", "tasks", task.tasks, 4),
            GLK_43499_PLAIN("task_struct", "seccomp", task.seccomp, 4),
            GLK_43499_PLAIN("cred", "copy_size", credential.copy_size, 4),
            GLK_43499_PLAIN("cred", "usage_offset", credential.usage_offset, 4),
            GLK_43499_PLAIN("cred", "usage_value", credential.usage_value, 4),
            GLK_43499_PLAIN("cred", "caps_offset", credential.caps_offset, 4),
            GLK_43499_PLAIN("cred", "caps_count", credential.caps_count, 4),
            GLK_43499_PLAIN("cred", "caps_value", credential.caps_value, 8),
            GLK_43499_PLAIN("cred", "ref_count", credential.ref_count, 4),
            GLK_43499_PLAIN("cred", "ref0_offset", credential.ref0_offset, 4),
            GLK_43499_PLAIN("cred", "ref1_offset", credential.ref1_offset, 4),
            GLK_43499_PLAIN("cred", "ref2_offset", credential.ref2_offset, 4),
            GLK_43499_PLAIN("cred", "ref3_offset", credential.ref3_offset, 4),
            GLK_43499_PLAIN("cred", "ref0_image", credential.ref0_image, 8),
            GLK_43499_PLAIN("cred", "ref1_image", credential.ref1_image, 8),
            GLK_43499_PLAIN("cred", "ref2_image", credential.ref2_image, 8),
            GLK_43499_PLAIN("cred", "ref3_image", credential.ref3_image, 8),
            GLK_43499_PLAIN("offset", "init_task", offsets.init_task, 8),
            GLK_43499_PLAIN("offset", "init_cred", offsets.init_cred, 8),
            GLK_43499_PLAIN("offset", "empty_zero_page", offsets.empty_zero_page, 8),
            GLK_43499_PLAIN("offset", "root_task_group", offsets.root_task_group, 8),
            GLK_43499_PLAIN("offset", "selinux_enforcing", offsets.selinux_enforcing, 8),
            GLK_43499_PLAIN("offset", "selinux_blob_sizes", offsets.selinux_blob_sizes, 8),
            GLK_43499_PLAIN("offset", "security_hook_heads", offsets.security_hook_heads, 8),
            GLK_43499_PLAIN("offset", "slide_nfulnl_logger", offsets.slide_nfulnl_logger, 8),
            GLK_43499_PLAIN("offset", "slide_loggers_0_1", offsets.slide_loggers_0_1, 8),
            GLK_43499_PLAIN("offset", "slide_boot_id", offsets.slide_boot_id, 8),
            GLK_43499_PLAIN("offset", "vr_sys_exit_tp", misc.vr_sys_exit_tp, 4),
            GLK_43499_PLAIN("execution.recommended_cpus", "main", execution.recommended_main_cpu, 4),
            GLK_43499_PLAIN("execution.recommended_cpus", "consumer", execution.recommended_consumer_cpu, 4),
            GLK_43499_PLAIN("execution.heap", "prepare_max_attempts", execution.heap_prepare_max_attempts, 4),
            GLK_43499_PLAIN("execution.heap", "prepare_timeout_ms", execution.heap_prepare_timeout_ms, 4),
            GLK_43499_PLAIN("execution.heap", "kernelsnitch_timeout_ms", execution.heap_kernelsnitch_timeout_ms, 4),
            GLK_43499_PLAIN("execution.race", "route_wait_ms", execution.race_route_wait_ms, 4),
            GLK_43499_PLAIN("execution.race", "route_done_timeout_ms", execution.race_route_done_timeout_ms, 4),
            GLK_43499_PLAIN("execution.race", "setup_settle_us", execution.race_setup_settle_us, 4),
            GLK_43499_PLAIN("execution.race", "state_poll_interval_us", execution.race_state_poll_interval_us, 4),
            GLK_43499_PLAIN("execution.stages", "w1_attempts", execution.w1_attempts, 4),
            GLK_43499_PLAIN("execution.stages", "w1_settle_us", execution.w1_settle_us, 4),
            GLK_43499_PLAIN("execution.stages", "w1_scratch_repair_attempts", execution.w1_scratch_repair_attempts, 4),
            GLK_43499_PLAIN("execution.stages", "w2_attempts", execution.w2_attempts, 4),
            GLK_43499_PLAIN("execution.stages", "w2_settle_us", execution.w2_settle_us, 4),
            GLK_43499_PLAIN("execution.stages", "w3_chain_rounds", execution.w3_chain_rounds, 4),
            GLK_43499_PLAIN("execution.stages", "w3_attempts", execution.w3_attempts, 4),
            GLK_43499_PLAIN("execution.stages", "w3_settle_us", execution.w3_settle_us, 4),
            GLK_43499_PLAIN("execution.handoff", "pre_dispatch_settle_ms", execution.handoff_pre_dispatch_settle_ms, 4),
            GLK_43499_PLAIN("execution.handoff", "module_poll_attempts", execution.handoff_module_poll_attempts, 4),
            GLK_43499_PLAIN("execution.handoff", "module_poll_interval_ms", execution.handoff_module_poll_interval_ms, 4),
            GLK_43499_PLAIN("execution.handoff", "enforce_poll_attempts", execution.handoff_enforce_poll_attempts, 4),
            GLK_43499_PLAIN("execution.handoff", "enforce_poll_interval_ms", execution.handoff_enforce_poll_interval_ms, 4),
            GLK_43499_PLAIN("execution.consumer", "max_calls", execution.select_consumer_max_calls, 4),
            GLK_43499_PLAIN("execution.consumer", "burst_calls", execution.select_consumer_burst_calls, 4),
            GLK_43499_PLAIN("route.tcp_zerocopy", "attempts", execution.tcp_attempts, 4),
            GLK_43499_PLAIN("route.tcp_zerocopy", "arm_sequence", execution.tcp_arm_sequence, 4),
            GLK_43499_PLAIN("route.tcp_zerocopy", "post_receive_hold_iterations", execution.tcp_post_receive_hold_iterations, 4),
            GLK_43499_PLAIN("route.select_stack", "enter_delay_us", execution.select_enter_delay_us, 4),
            GLK_43499_PLAIN("route.select_stack", "timeout_us", execution.select_timeout_us, 4),
            GLK_43499_PLAIN("route.multicast_waiter", "attempts", mcast_attempts, 1),
            GLK_43499_PLAIN("route.multicast_waiter", "arm_sequence", mcast_arm_sequence, 1),
            GLK_43499_PLAIN("route.multicast_waiter", "arm_hold", mcast_arm_hold, 2),
            GLK_43499_PLAIN("vr_guard", "tracepoint_funcs", misc.vr_tracepoint_funcs, 1),
            GLK_43499_OPT("kernel", "kernel_phys_load", misc.kernel_phys_load, 8, false),
            GLK_43499_OPT("kernel", "kernel_phys_offset", misc.kernel_phys_offset, 8, false),
            GLK_43499_OPT("kernel", "compact_waiter", misc.compact_waiter, 1, false),
            GLK_43499_OPT("kernel", "kernelsnitch_collisions", misc.kernelsnitch_collisions, 4, false),
            GLK_43499_OPT("kernel", "mm_struct_sz", misc.mm_struct_sz, 4, false),
            GLK_43499_OPT("route.tcp_zerocopy", "payload_delta", geometry.tcp_payload_delta, 8, true),
            GLK_43499_OPT("route.tcp_zerocopy", "chunk_bias", geometry.tcp_chunk_bias, 8, false),
            GLK_43499_OPT("route.tcp_zerocopy", "fake_task_off", geometry.tcp_fake_task_off, 8, false),
            GLK_43499_OPT("route.tcp_zerocopy", "cred_copy_off", geometry.tcp_cred_copy_off, 8, false),
            GLK_43499_OPT("route.select_stack", "waiter_shift", geometry.pselect_waiter_shift, 4, true),
            GLK_43499_OPT("route.select_stack", "compact_waiter", misc.compact_waiter, 1, false),
            GLK_43499_OPT("route.multicast_waiter", "waiter_off", geometry.mcast_waiter_off, 4, true),
            GLK_43499_OPT("route.multicast_waiter", "buffer_size", geometry.mcast_buffer_size, 4, false),
            GLK_43499_OPT("route.multicast_waiter", "task_offset", geometry.mcast_task_offset, 4, false),
            GLK_43499_OPT("route.multicast_waiter", "lock_offset", geometry.mcast_lock_offset, 4, false),
            GLK_43499_STEPS("backend.cve_2026_43499", "steps"),
        };
    };

#undef GLK_43499_PLAIN
#undef GLK_43499_OPT
#undef GLK_43499_STEPS

} // namespace ghostlock::backend

#endif
