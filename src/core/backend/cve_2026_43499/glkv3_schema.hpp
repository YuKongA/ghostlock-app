#ifndef GHOSTLOCK_BACKEND_CVE_2026_43499_GLKV3_SCHEMA_HPP
#define GHOSTLOCK_BACKEND_CVE_2026_43499_GLKV3_SCHEMA_HPP

/* GLKv3 (MessagePack) path -> type declaration for the cve_2026_43499 backend
 * keys (GLKv3-3, ADR-0003 wire migration; split by A2-4-3).
 *
 * kCve2026_43499Glkv3Fields is the single GLKv3 statement of every (section,key)
 * this backend owner interprets; the platform ABI keys live in
 * platform::abi (kPlatformAbiGlkv3Fields). It mirrors schema.hpp's v2 owner
 * Schema one-for-one: the key names are byte-for-byte identical and the set of
 * (section,key) pairs is equal (asserted by glkv3_schema_test), so the v2 field
 * table and the v3 type table cannot drift while the migration is in flight.
 *
 * S4 R2 owner-qualified sections: this owner's paths are
 * backend.cve_2026_43499.* (route/execution/cred/offset/kernel/steps). The
 * HOCON refactor deleted the "common" owner: kernel_major / kernel_minor /
 * safe_mode are ROOT scalars now, declared here with the empty section (the
 * root marker the codec and profile/document.hpp kRootSection share), and the
 * vivo countermeasure key is gone with the (now empty) countermeasure owner.
 *
 * WireType is chosen from the semantic type of the v2 field (see
 * docs/analysis/wire-transport-model.md section 4):
 *   - boolean flags   -> Bool  (root safe_mode,
 *                               backend.cve_2026_43499.kernel.compact_waiter,
 *                               backend.cve_2026_43499.route.select_stack.compact_waiter);
 *   - signed geometry -> Int   (...route.tcp_zerocopy.payload_delta,
 *                               ...route.select_stack.waiter_shift,
 *                               ...route.multicast_waiter.waiter_off);
 *   - every other v2 field (offsets, counts, tokens, KMI) -> UInt.
 *
 * Every field is optional: presence is expressed by key occurrence, matching the
 * v2 owner Schema where required is false for all fields. */

#include "profile/glkv3.hpp"

namespace ghostlock::backend {
    inline constexpr profile::glkv3::FieldSpec kCve2026_43499Glkv3Fields[] = {
        /* HOCON refactor root scalars: section "" is the document root. */
        {"", "kernel_major", profile::glkv3::WireType::UInt, false},
        {"", "kernel_minor", profile::glkv3::WireType::UInt, false},
        {"", "safe_mode", profile::glkv3::WireType::Bool, false},
        {"backend.cve_2026_43499.cred", "copy_size", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.cred", "usage_value", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.cred", "caps_count", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.cred", "caps_value", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.cred", "ref0_image", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.cred", "ref1_image", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.cred", "ref2_image", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.cred", "ref3_image", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.offset", "slide_nfulnl_logger", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.offset", "slide_loggers_0_1", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.offset", "slide_boot_id", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.offset", "vr_sys_exit_tp", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.execution.recommended_cpus", "main", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.execution.recommended_cpus", "consumer", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.execution.heap", "prepare_max_attempts", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.execution.heap", "prepare_timeout_ms", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.execution.heap", "kernelsnitch_timeout_ms", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.execution.race", "route_wait_ms", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.execution.race", "route_done_timeout_ms", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.execution.race", "setup_settle_us", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.execution.race", "state_poll_interval_us", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.execution.stages", "w1_attempts", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.execution.stages", "w1_settle_us", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.execution.stages", "w1_scratch_repair_attempts", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.execution.stages", "w2_attempts", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.execution.stages", "w2_settle_us", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.execution.stages", "w3_chain_rounds", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.execution.stages", "w3_attempts", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.execution.stages", "w3_settle_us", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.execution.handoff", "pre_dispatch_settle_ms", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.execution.handoff", "module_poll_attempts", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.execution.handoff", "module_poll_interval_ms", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.execution.handoff", "enforce_poll_attempts", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.execution.handoff", "enforce_poll_interval_ms", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.execution.consumer", "max_calls", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.execution.consumer", "burst_calls", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.route.tcp_zerocopy", "attempts", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.route.tcp_zerocopy", "arm_sequence", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.route.tcp_zerocopy", "post_receive_hold_iterations", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.route.tcp_zerocopy", "payload_delta", profile::glkv3::WireType::Int, false},
        {"backend.cve_2026_43499.route.tcp_zerocopy", "chunk_bias", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.route.tcp_zerocopy", "fake_task_off", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.route.tcp_zerocopy", "cred_copy_off", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.route.select_stack", "enter_delay_us", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.route.select_stack", "timeout_us", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.route.select_stack", "waiter_shift", profile::glkv3::WireType::Int, false},
        {"backend.cve_2026_43499.route.select_stack", "compact_waiter", profile::glkv3::WireType::Bool, false},
        {"backend.cve_2026_43499.route.multicast_waiter", "attempts", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.route.multicast_waiter", "arm_sequence", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.route.multicast_waiter", "arm_hold", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.route.multicast_waiter", "waiter_off", profile::glkv3::WireType::Int, false},
        {"backend.cve_2026_43499.route.multicast_waiter", "buffer_size", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.route.multicast_waiter", "task_offset", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.route.multicast_waiter", "lock_offset", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.kernel", "compact_waiter", profile::glkv3::WireType::Bool, false},
        {"backend.cve_2026_43499.kernel", "kernelsnitch_collisions", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499.kernel", "mm_struct_sz", profile::glkv3::WireType::UInt, false},
        {"backend.cve_2026_43499", "steps", profile::glkv3::WireType::Str, false},
    };

    inline constexpr profile::glkv3::Schema kCve2026_43499Glkv3Schema{
            kCve2026_43499Glkv3Fields};
} // namespace ghostlock::backend

#endif
