#pragma once
/** @file profile/key_catalog.hpp
 *  The profile keys, as one table: what the manifest is generated from.
 *
 *  ADOPTED (2026-10-08): the rows below are the content the two hand-maintained
 *  profile-manifest-v3.tsv copies already carried, extracted once so that from here on
 *  the file is generated rather than copied. This table is now the authority: a key is
 *  added here and the manifest regenerated, which is what the rules require of a list
 *  that crosses between native and the app.
 */
#include <cstddef>
#include <cstdio>
#include <string_view>

namespace ghostlock::profile {
    /** One declared key: the manifest row, column for column. */
    struct KeyRow final {
        std::string_view owner;
        std::string_view path;
        std::string_view wire;
        std::string_view required;
        std::string_view by_default;
        std::string_view source;
        std::string_view doc;
        std::string_view width;
    };

    /** The header comments the export carries, in order. */
    inline constexpr std::string_view kManifestHeader[] = {
        "# GhostLock GLKv3 owner-qualified manifest (GLKv3 schema 3 / generated form R2).",
        "# Authoritative native export; regenerate with:",
        "#   make -C src profile-manifest-v3",
        "# Columns: owner<TAB>path<TAB>wire<TAB>required<TAB>default<TAB>source<TAB>doc<TAB>width",
        "# The width column is the v2 owner declaration's own bit width (1/2/4/8),# never hand-copied, and it is a HARD VALIDATION INPUT: the Kotlin adapter# must reject a raw numeric value that does not fit the declared width# (e.g. 2^40 in a u32 field). str/array carry no bit width and export \"-\";# bool always exports 1. The producer asserts width<->wire consistency.",
        "# The wire column may be a \"|\"-separated UNION of wire kinds (the S4 P1",
        "# dynamic plugin paths plugin.<id>.params.* / plugin.<id>.extract.*); every",
        "# member must come from the wire-kind vocabulary and an unknown member is",
        "# rejected fail-closed (the Kotlin adapter parses the same union).",
        "# Dynamic paths resolve their concrete type from the plugin descriptor",
        "# (probe TSV param/extract rows), whose type literals are these same kinds.",
    };
    inline constexpr std::size_t kManifestHeaderCount =
        sizeof(kManifestHeader) / sizeof(kManifestHeader[0]);

    /** The rows, in the order they are exported. */
    inline constexpr KeyRow kKeyRows[] = {
        {"cve_2026_43284", "backend.cve_2026_43284.execution.late_load_args", "uint", "0", "literal:0", "profile", "late-load argument policy bitmask; 0 = program + late-load only.", "8"},
        {"cve_2026_43284", "backend.cve_2026_43284.execution.module_poll_attempts", "uint", "0", "literal:40", "profile", "LKM window handshake retries; default 40.", "4"},
        {"cve_2026_43284", "backend.cve_2026_43284.execution.module_poll_interval_ms", "uint", "0", "literal:5", "profile", "LKM window handshake retry delay in ms; default 5.", "4"},
        {"cve_2026_43284", "backend.cve_2026_43284.execution.selinux_exec_context", "uint", "0", "literal:0", "profile", "SELinux exec context selector; 0 = vendor_modprobe.", "8"},
        {"cve_2026_43284", "backend.cve_2026_43284.execution.wait_timeout_ms", "uint", "0", "literal:15000", "profile", "Chain terminus wait budget in ms; default 15000.", "4"},
        {"cve_2026_43284", "backend.cve_2026_43284.experimental", "bool", "0", "-", "profile", "Static experimental opt-in declaration (U5).", "1"},
        {"cve_2026_43284", "backend.cve_2026_43284.queue", "array", "0", "-", "profile", "Step queue: array of {step[,stage]} (M2).", "-"},
        {"cve_2026_43284", "backend.cve_2026_43284.route", "str", "0", "-", "profile", "Queue-level route token (M2; forbidden for this backend).", "-"},
        {"cve_2026_43284", "backend.cve_2026_43284.steps", "str", "0", "-", "profile", "REMOVED selection surface (M5): a token here is REFUSED (reason=token-form-removed).", "-"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.cred.caps_offset", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.cred.ref0_offset", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.cred.ref1_offset", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.cred.ref2_offset", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.cred.ref3_offset", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.payload.lock_offset", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.payload.waiter_w0_offset", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.payload.fake_task_offset", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.payload.cred_copy_offset", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.payload.fops_table_offset", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.cred.ref_count", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.cred.usage_offset", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.kernel.kernel_phys_load", "uint", "0", "-", "profile", "-", "8"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.kernel.kernel_phys_offset", "uint", "0", "-", "profile", "-", "8"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.offset.empty_zero_page", "uint", "0", "-", "profile", "-", "8"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.offset.init_cred", "uint", "0", "-", "profile", "-", "8"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.offset.init_task", "uint", "0", "-", "profile", "-", "8"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.offset.root_task_group", "uint", "0", "-", "profile", "-", "8"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.offset.security_hook_heads", "uint", "0", "-", "profile", "-", "8"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.offset.selinux_blob_sizes", "uint", "0", "-", "profile", "-", "8"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.offset.selinux_enforcing", "uint", "0", "-", "profile", "-", "8"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.task_struct.atomic_flags", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.task_struct.comm", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.task_struct.cred", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.task_struct.normal_prio", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.task_struct.pi_blocked_on", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.task_struct.pi_lock", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.task_struct.pi_top_task", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.task_struct.pi_waiters", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.task_struct.pid", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.task_struct.prio", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.task_struct.real_cred", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.task_struct.sched_task_group", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.task_struct.seccomp", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.task_struct.tasks", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.abi.task_struct.tgid", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.cred.caps_count", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.cred.caps_value", "uint", "0", "-", "profile", "-", "8"},
        {"cve_2026_43499", "backend.cve_2026_43499.cred.copy_size", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.cred.ref0_image", "uint", "0", "-", "profile", "-", "8"},
        {"cve_2026_43499", "backend.cve_2026_43499.cred.ref1_image", "uint", "0", "-", "profile", "-", "8"},
        {"cve_2026_43499", "backend.cve_2026_43499.cred.ref2_image", "uint", "0", "-", "profile", "-", "8"},
        {"cve_2026_43499", "backend.cve_2026_43499.cred.ref3_image", "uint", "0", "-", "profile", "-", "8"},
        {"cve_2026_43499", "backend.cve_2026_43499.cred.usage_value", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.execution.consumer.burst_calls", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.execution.consumer.max_calls", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.execution.handoff.enforce_poll_attempts", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.execution.handoff.enforce_poll_interval_ms", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.execution.handoff.module_poll_attempts", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.execution.handoff.module_poll_interval_ms", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.execution.handoff.pre_dispatch_settle_ms", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.execution.heap.kernelsnitch_collisions", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.execution.heap.kernelsnitch_timeout_ms", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.execution.heap.prepare_max_attempts", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.execution.heap.prepare_timeout_ms", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.execution.heap.snitch_round_wait_us", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.execution.heap.snitch_threads", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.execution.heap.snitch_wake_rounds", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.execution.race.route_done_timeout_ms", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.execution.race.route_wait_ms", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.execution.race.setup_settle_us", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.execution.race.state_poll_interval_us", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.execution.recommended_cpus.consumer", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.execution.recommended_cpus.main", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.execution.stages.w1_attempts", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.execution.stages.w1_scratch_repair_attempts", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.execution.stages.w1_settle_us", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.execution.stages.w2_attempts", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.execution.stages.w2_settle_us", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.execution.stages.w3_attempts", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.execution.stages.w3_chain_rounds", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.execution.stages.w3_settle_us", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.experimental", "bool", "0", "-", "profile", "Static experimental opt-in declaration (U5).", "1"},
        {"cve_2026_43499", "backend.cve_2026_43499.kernel.compact_waiter", "bool", "0", "-", "profile", "-", "1"},
        {"cve_2026_43499", "backend.cve_2026_43499.kernel.kernelsnitch_collisions", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.kernel.mm_struct_sz", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.offset.slide_boot_id", "uint", "0", "-", "profile", "-", "8"},
        {"cve_2026_43499", "backend.cve_2026_43499.offset.slide_loggers_0_1", "uint", "0", "-", "profile", "-", "8"},
        {"cve_2026_43499", "backend.cve_2026_43499.offset.slide_nfulnl_logger", "uint", "0", "-", "profile", "-", "8"},
        {"cve_2026_43499", "backend.cve_2026_43499.offset.vr_sys_exit_tp", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.queue", "array", "0", "-", "profile", "Step queue: array of {step[,stage]} (M2).", "-"},
        {"cve_2026_43499", "backend.cve_2026_43499.route", "str", "0", "-", "profile", "Queue-level route token (M2; required here).", "-"},
        {"cve_2026_43499", "backend.cve_2026_43499.route.multicast_waiter.arm_hold", "uint", "0", "-", "profile", "-", "2"},
        {"cve_2026_43499", "backend.cve_2026_43499.route.multicast_waiter.arm_sequence", "uint", "0", "-", "profile", "-", "1"},
        {"cve_2026_43499", "backend.cve_2026_43499.route.multicast_waiter.attempts", "uint", "0", "-", "profile", "-", "1"},
        {"cve_2026_43499", "backend.cve_2026_43499.route.multicast_waiter.buffer_size", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.route.multicast_waiter.lock_offset", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.route.multicast_waiter.task_offset", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.route.multicast_waiter.waiter_off", "int", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.route.select_stack.compact_waiter", "bool", "0", "-", "profile", "-", "1"},
        {"cve_2026_43499", "backend.cve_2026_43499.route.select_stack.enter_delay_us", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.route.select_stack.timeout_us", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.route.select_stack.waiter_shift", "int", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.route.tcp_zerocopy.arm_sequence", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.route.tcp_zerocopy.attempts", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.route.tcp_zerocopy.chunk_bias", "uint", "0", "-", "profile", "-", "8"},
        {"cve_2026_43499", "backend.cve_2026_43499.route.tcp_zerocopy.cred_copy_off", "uint", "0", "-", "profile", "-", "8"},
        {"cve_2026_43499", "backend.cve_2026_43499.route.tcp_zerocopy.fake_task_off", "uint", "0", "-", "profile", "-", "8"},
        {"cve_2026_43499", "backend.cve_2026_43499.route.tcp_zerocopy.payload_delta", "int", "0", "-", "profile", "-", "8"},
        {"cve_2026_43499", "backend.cve_2026_43499.route.tcp_zerocopy.post_receive_hold_iterations", "uint", "0", "-", "profile", "-", "4"},
        {"cve_2026_43499", "backend.cve_2026_43499.steps", "str", "0", "-", "profile", "REMOVED selection surface (M5): a token here is REFUSED (reason=token-form-removed).", "-"},
        {"plugin", "plugin.<id>.enabled", "bool", "0", "literal:0", "profile", "Plugin enabled; default false, and only enabled=true is emitted.", "1"},
        {"plugin", "plugin.<id>.extract.*", "uint|int|bool|str", "0", "-", "profile", "Dynamic: P2 extractor writes it; P1 validates shape only.", "-"},
        {"plugin", "plugin.<id>.module_hash", "str", "0", "-", "profile", "Lowercase hex SHA-256 of the module file.", "-"},
        {"plugin", "plugin.<id>.module_path", "str", "0", "-", "profile", "Module path RELATIVE to <GHOSTLOCK_HOME>/countermeasures.", "-"},
        {"plugin", "plugin.<id>.params.*", "uint|int|bool|str", "0", "-", "profile", "Dynamic: type comes from the plugin descriptor (probe TSV param rows).", "-"},
        {"plugin", "plugin.<id>.stage", "str", "0", "-", "profile", "Hook stage token (pre_spawn|post_spawn|pre_terminal|post_terminal).", "-"},
        {"root", "kernel_major", "uint", "0", "-", "profile", "-", "1"},
        {"root", "kernel_minor", "uint", "0", "-", "profile", "-", "1"},
        {"root", "safe_mode", "bool", "0", "-", "profile", "-", "1"},
    };
    inline constexpr std::size_t kKeyRowCount = sizeof(kKeyRows) / sizeof(kKeyRows[0]);

    /** Write the manifest exactly as the shipped copies carry it. */
    [[nodiscard]] bool write_manifest(std::FILE *out) noexcept;
}  // namespace ghostlock::profile

