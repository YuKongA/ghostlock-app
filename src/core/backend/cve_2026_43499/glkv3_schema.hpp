#ifndef GHOSTLOCK_BACKEND_CVE_2026_43499_GLKV3_SCHEMA_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43499_GLKV3_SCHEMA_HPP

/* GLKv3 (MessagePack) path -> type declaration for the cve_2026_43499 owner
 * (GLKv3-3, ADR-0003 wire migration).
 *
 * kCve2026_43499Glkv3Fields is the single GLKv3 statement of every (section,key)
 * this owner reads. It mirrors schema.hpp's v2 owner Schema one-for-one: the key
 * names are byte-for-byte identical and the set of (section,key) pairs is equal
 * (asserted by glkv3_schema_test), so the v2 field table and the v3 type table
 * cannot drift while the migration is in flight.
 *
 * WireType is chosen from the semantic type of the v2 field (see
 * docs/analysis/wire-transport-model.md section 4):
 *   - boolean flags   -> Bool  (meta.safe_mode, meta.vr_guard, kernel.compact_waiter,
 *                               route.select_stack.compact_waiter);
 *   - signed geometry -> Int   (route.tcp_zerocopy.payload_delta,
 *                               route.select_stack.waiter_shift,
 *                               route.multicast_waiter.waiter_off);
 *   - every other v2 field (offsets, counts, tokens, KMI) -> UInt.
 *
 * Every field is optional: presence is expressed by key occurrence, matching the
 * v2 owner Schema where required is false for all fields. */

#include "profile/glkv3.hpp"

namespace ghostlock::backend {
    inline constexpr profile::glkv3::FieldSpec kCve2026_43499Glkv3Fields[] = {
        {"meta", "kernel_major", profile::glkv3::WireType::UInt, false},
        {"meta", "fallback_route", profile::glkv3::WireType::UInt, false},
        {"meta", "safe_mode", profile::glkv3::WireType::Bool, false},
        {"meta", "vr_guard", profile::glkv3::WireType::Bool, false},
        {"task_struct", "prio", profile::glkv3::WireType::UInt, false},
        {"task_struct", "normal_prio", profile::glkv3::WireType::UInt, false},
        {"task_struct", "sched_task_group", profile::glkv3::WireType::UInt, false},
        {"task_struct", "pi_lock", profile::glkv3::WireType::UInt, false},
        {"task_struct", "pi_waiters", profile::glkv3::WireType::UInt, false},
        {"task_struct", "pi_top_task", profile::glkv3::WireType::UInt, false},
        {"task_struct", "pi_blocked_on", profile::glkv3::WireType::UInt, false},
        {"task_struct", "pid", profile::glkv3::WireType::UInt, false},
        {"task_struct", "tgid", profile::glkv3::WireType::UInt, false},
        {"task_struct", "atomic_flags", profile::glkv3::WireType::UInt, false},
        {"task_struct", "real_cred", profile::glkv3::WireType::UInt, false},
        {"task_struct", "cred", profile::glkv3::WireType::UInt, false},
        {"task_struct", "comm", profile::glkv3::WireType::UInt, false},
        {"task_struct", "tasks", profile::glkv3::WireType::UInt, false},
        {"task_struct", "seccomp", profile::glkv3::WireType::UInt, false},
        {"cred", "copy_size", profile::glkv3::WireType::UInt, false},
        {"cred", "usage_offset", profile::glkv3::WireType::UInt, false},
        {"cred", "usage_value", profile::glkv3::WireType::UInt, false},
        {"cred", "caps_offset", profile::glkv3::WireType::UInt, false},
        {"cred", "caps_count", profile::glkv3::WireType::UInt, false},
        {"cred", "caps_value", profile::glkv3::WireType::UInt, false},
        {"cred", "ref_count", profile::glkv3::WireType::UInt, false},
        {"cred", "ref0_offset", profile::glkv3::WireType::UInt, false},
        {"cred", "ref1_offset", profile::glkv3::WireType::UInt, false},
        {"cred", "ref2_offset", profile::glkv3::WireType::UInt, false},
        {"cred", "ref3_offset", profile::glkv3::WireType::UInt, false},
        {"cred", "ref0_image", profile::glkv3::WireType::UInt, false},
        {"cred", "ref1_image", profile::glkv3::WireType::UInt, false},
        {"cred", "ref2_image", profile::glkv3::WireType::UInt, false},
        {"cred", "ref3_image", profile::glkv3::WireType::UInt, false},
        {"offset", "init_task", profile::glkv3::WireType::UInt, false},
        {"offset", "init_cred", profile::glkv3::WireType::UInt, false},
        {"offset", "empty_zero_page", profile::glkv3::WireType::UInt, false},
        {"offset", "root_task_group", profile::glkv3::WireType::UInt, false},
        {"offset", "selinux_enforcing", profile::glkv3::WireType::UInt, false},
        {"offset", "selinux_blob_sizes", profile::glkv3::WireType::UInt, false},
        {"offset", "security_hook_heads", profile::glkv3::WireType::UInt, false},
        {"offset", "slide_nfulnl_logger", profile::glkv3::WireType::UInt, false},
        {"offset", "slide_loggers_0_1", profile::glkv3::WireType::UInt, false},
        {"offset", "slide_boot_id", profile::glkv3::WireType::UInt, false},
        {"offset", "vr_sys_exit_tp", profile::glkv3::WireType::UInt, false},
        {"execution.recommended_cpus", "main", profile::glkv3::WireType::UInt, false},
        {"execution.recommended_cpus", "consumer", profile::glkv3::WireType::UInt, false},
        {"execution.heap", "prepare_max_attempts", profile::glkv3::WireType::UInt, false},
        {"execution.heap", "prepare_timeout_ms", profile::glkv3::WireType::UInt, false},
        {"execution.heap", "kernelsnitch_timeout_ms", profile::glkv3::WireType::UInt, false},
        {"execution.race", "route_wait_ms", profile::glkv3::WireType::UInt, false},
        {"execution.race", "route_done_timeout_ms", profile::glkv3::WireType::UInt, false},
        {"execution.race", "setup_settle_us", profile::glkv3::WireType::UInt, false},
        {"execution.race", "state_poll_interval_us", profile::glkv3::WireType::UInt, false},
        {"execution.stages", "w1_attempts", profile::glkv3::WireType::UInt, false},
        {"execution.stages", "w1_settle_us", profile::glkv3::WireType::UInt, false},
        {"execution.stages", "w1_scratch_repair_attempts", profile::glkv3::WireType::UInt, false},
        {"execution.stages", "w2_attempts", profile::glkv3::WireType::UInt, false},
        {"execution.stages", "w2_settle_us", profile::glkv3::WireType::UInt, false},
        {"execution.stages", "w3_chain_rounds", profile::glkv3::WireType::UInt, false},
        {"execution.stages", "w3_attempts", profile::glkv3::WireType::UInt, false},
        {"execution.stages", "w3_settle_us", profile::glkv3::WireType::UInt, false},
        {"execution.handoff", "pre_dispatch_settle_ms", profile::glkv3::WireType::UInt, false},
        {"execution.handoff", "module_poll_attempts", profile::glkv3::WireType::UInt, false},
        {"execution.handoff", "module_poll_interval_ms", profile::glkv3::WireType::UInt, false},
        {"execution.handoff", "enforce_poll_attempts", profile::glkv3::WireType::UInt, false},
        {"execution.handoff", "enforce_poll_interval_ms", profile::glkv3::WireType::UInt, false},
        {"execution.consumer", "max_calls", profile::glkv3::WireType::UInt, false},
        {"execution.consumer", "burst_calls", profile::glkv3::WireType::UInt, false},
        {"route.tcp_zerocopy", "attempts", profile::glkv3::WireType::UInt, false},
        {"route.tcp_zerocopy", "arm_sequence", profile::glkv3::WireType::UInt, false},
        {"route.tcp_zerocopy", "post_receive_hold_iterations", profile::glkv3::WireType::UInt, false},
        {"route.tcp_zerocopy", "payload_delta", profile::glkv3::WireType::Int, false},
        {"route.tcp_zerocopy", "chunk_bias", profile::glkv3::WireType::UInt, false},
        {"route.tcp_zerocopy", "fake_task_off", profile::glkv3::WireType::UInt, false},
        {"route.tcp_zerocopy", "cred_copy_off", profile::glkv3::WireType::UInt, false},
        {"route.select_stack", "enter_delay_us", profile::glkv3::WireType::UInt, false},
        {"route.select_stack", "timeout_us", profile::glkv3::WireType::UInt, false},
        {"route.select_stack", "waiter_shift", profile::glkv3::WireType::Int, false},
        {"route.select_stack", "compact_waiter", profile::glkv3::WireType::Bool, false},
        {"route.multicast_waiter", "attempts", profile::glkv3::WireType::UInt, false},
        {"route.multicast_waiter", "arm_sequence", profile::glkv3::WireType::UInt, false},
        {"route.multicast_waiter", "arm_hold", profile::glkv3::WireType::UInt, false},
        {"route.multicast_waiter", "waiter_off", profile::glkv3::WireType::Int, false},
        {"route.multicast_waiter", "buffer_size", profile::glkv3::WireType::UInt, false},
        {"route.multicast_waiter", "task_offset", profile::glkv3::WireType::UInt, false},
        {"route.multicast_waiter", "lock_offset", profile::glkv3::WireType::UInt, false},
        {"vr_guard", "tracepoint_funcs", profile::glkv3::WireType::UInt, false},
        {"kernel", "kernel_phys_load", profile::glkv3::WireType::UInt, false},
        {"kernel", "kernel_phys_offset", profile::glkv3::WireType::UInt, false},
        {"kernel", "compact_waiter", profile::glkv3::WireType::Bool, false},
        {"kernel", "kernelsnitch_collisions", profile::glkv3::WireType::UInt, false},
        {"kernel", "mm_struct_sz", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499", "steps", profile::glkv3::WireType::UInt, false},
    };

    inline constexpr profile::glkv3::Schema kCve2026_43499Glkv3Schema{
            kCve2026_43499Glkv3Fields};
} // namespace ghostlock::backend

#endif
