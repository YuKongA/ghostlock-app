package com.ghostlock.app.ui

import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import com.ghostlock.app.R

/** Localized display name for a field path, falling back to the key. */
@Composable
internal fun fieldLabel(path: String, fallback: String): String =
    fieldLabelRes(path)?.let { stringResource(it) } ?: fallback

/**
 * Every logical editor path that has a dedicated label, with its string
 * resource. The map is the single source for [fieldLabelRes]; it is exposed to
 * the tests so FieldLabelsManifestAgreementTest can prove every label names a
 * field the native manifest actually declares (a label must never orphan).
 */
internal val fieldLabels: Map<String, Int> = mapOf(
    "route.multicast_waiter.compact_waiter" to R.string.field_compact_waiter,
    "route.tcp_zerocopy.compact_waiter" to R.string.field_compact_waiter,
    "cred.caps_count" to R.string.field_cred_caps_count,
    "cred.caps_offset" to R.string.field_cred_caps_offset,
    "cred.caps_value" to R.string.field_cred_caps_value,
    "cred.copy_size" to R.string.field_cred_copy_size,
    "cred.ref0_image" to R.string.field_cred_ref0_image,
    "cred.ref0_offset" to R.string.field_cred_ref0_offset,
    "cred.ref1_image" to R.string.field_cred_ref1_image,
    "cred.ref1_offset" to R.string.field_cred_ref1_offset,
    "cred.ref2_image" to R.string.field_cred_ref2_image,
    "cred.ref2_offset" to R.string.field_cred_ref2_offset,
    "cred.ref3_image" to R.string.field_cred_ref3_image,
    "cred.ref3_offset" to R.string.field_cred_ref3_offset,
    "cred.ref_count" to R.string.field_cred_ref_count,
    "cred.usage_value" to R.string.field_cred_usage_value,
    "execution.handoff.enforce_poll_attempts" to R.string.field_execution_handoff_enforce_poll_attempts,
    "execution.handoff.enforce_poll_interval_ms" to R.string.field_execution_handoff_enforce_poll_interval_ms,
    "execution.handoff.module_poll_attempts" to R.string.field_execution_handoff_module_poll_attempts,
    "execution.handoff.module_poll_interval_ms" to R.string.field_execution_handoff_module_poll_interval_ms,
    "execution.handoff.pre_dispatch_settle_ms" to R.string.field_execution_handoff_pre_dispatch_settle_ms,
    "execution.heap.kernelsnitch_timeout_ms" to R.string.field_execution_heap_kernelsnitch_timeout_ms,
    "execution.heap.prepare_max_attempts" to R.string.field_execution_heap_prepare_max_attempts,
    "execution.heap.prepare_timeout_ms" to R.string.field_execution_heap_prepare_timeout_ms,
    "execution.race.route_wait_ms" to R.string.field_execution_race_route_wait_ms,
    "execution.race.route_done_timeout_ms" to R.string.field_execution_race_route_done_timeout_ms,
    "execution.race.setup_settle_us" to R.string.field_execution_race_setup_settle_us,
    "execution.race.state_poll_interval_us" to R.string.field_execution_race_state_poll_interval_us,
    "execution.recommended_cpus.consumer" to R.string.field_execution_recommended_cpus_consumer,
    "execution.recommended_cpus.main" to R.string.field_execution_recommended_cpus_main,
    "execution.routes.select_stack.consumer_burst_calls" to R.string.field_execution_routes_select_stack_consumer_burst_calls,
    "execution.routes.select_stack.consumer_max_calls" to R.string.field_execution_routes_select_stack_consumer_max_calls,
    "execution.routes.select_stack.enter_delay_us" to R.string.field_execution_routes_select_stack_enter_delay_us,
    "execution.routes.select_stack.timeout_us" to R.string.field_execution_routes_select_stack_timeout_us,
    "execution.routes.tcp_zerocopy.arm_sequence" to R.string.field_execution_routes_tcp_zerocopy_arm_sequence,
    "execution.routes.tcp_zerocopy.attempts" to R.string.field_execution_routes_tcp_zerocopy_attempts,
    "execution.routes.tcp_zerocopy.post_receive_hold_iterations" to R.string.field_execution_routes_tcp_zerocopy_post_receive_hold_iterations,
    "execution.selected_cpus.consumer" to R.string.field_execution_selected_cpus_consumer,
    "execution.selected_cpus.main" to R.string.field_execution_selected_cpus_main,
    "execution.stages.w1_attempts" to R.string.field_execution_stages_w1_attempts,
    "execution.stages.w1_scratch_repair_attempts" to R.string.field_execution_stages_w1_scratch_repair_attempts,
    "execution.stages.w1_settle_us" to R.string.field_execution_stages_w1_settle_us,
    "execution.stages.w2_attempts" to R.string.field_execution_stages_w2_attempts,
    "execution.stages.w2_settle_us" to R.string.field_execution_stages_w2_settle_us,
    "execution.stages.w3_attempts" to R.string.field_execution_stages_w3_attempts,
    "execution.stages.w3_chain_rounds" to R.string.field_execution_stages_w3_chain_rounds,
    "execution.stages.w3_settle_us" to R.string.field_execution_stages_w3_settle_us,
    "kernel_major" to R.string.field_kernel_major,
    "kernelsnitch.collisions" to R.string.field_kernelsnitch_collisions,
    "kernelsnitch.mm_struct_sz" to R.string.field_mm_struct_sz,
    "kernel_phys_load" to R.string.field_kernel_phys_load,
    "kernel_phys_offset" to R.string.field_kernel_phys_offset,
    "route.multicast_waiter.arm_hold" to R.string.field_mcast_arm_hold,
    "route.multicast_waiter.arm_sequence" to R.string.field_mcast_arm_sequence,
    "route.multicast_waiter.attempts" to R.string.field_mcast_attempts,
    "route.multicast_waiter.buffer_size" to R.string.field_mcast_buffer_size,
    "route.multicast_waiter.lock_offset" to R.string.field_mcast_lock_offset,
    "route.multicast_waiter.task_offset" to R.string.field_mcast_task_offset,
    "route.multicast_waiter.waiter_off" to R.string.field_mcast_waiter_off,
    "offset.empty_zero_page" to R.string.field_off_empty_zero_page,
    "offset.init_cred" to R.string.field_off_init_cred,
    "offset.init_task" to R.string.field_off_init_task,
    "offset.root_task_group" to R.string.field_off_root_task_group,
    "offset.security_hook_heads" to R.string.field_off_security_hook_heads,
    "offset.selinux_blob_sizes" to R.string.field_off_selinux_blob_sizes,
    "offset.selinux_enforcing" to R.string.field_off_selinux_enforcing,
    "offset.slide_boot_id" to R.string.field_off_slide_boot_id,
    "offset.slide_loggers_0_1" to R.string.field_off_slide_loggers_0_1,
    "offset.slide_nfulnl_logger" to R.string.field_off_slide_nfulnl_logger,
    "route.select_stack.waiter_shift" to R.string.field_pselect_waiter_shift,
    "task_struct.atomic_flags" to R.string.field_task_atomic_flags,
    "task_struct.comm" to R.string.field_task_comm,
    "task_struct.cred" to R.string.field_task_cred,
    "task_struct.normal_prio" to R.string.field_task_normal_prio,
    "task_struct.pi_blocked_on" to R.string.field_task_pi_blocked_on,
    "task_struct.pi_lock" to R.string.field_task_pi_lock,
    "task_struct.pi_top_task" to R.string.field_task_pi_top_task,
    "task_struct.pi_waiters" to R.string.field_task_pi_waiters,
    "task_struct.pid" to R.string.field_task_pid,
    "task_struct.prio" to R.string.field_task_prio,
    "task_struct.real_cred" to R.string.field_task_real_cred,
    "task_struct.sched_task_group" to R.string.field_task_sched_task_group,
    "task_struct.seccomp" to R.string.field_task_seccomp,
    "task_struct.tasks" to R.string.field_task_tasks,
    "task_struct.tgid" to R.string.field_task_tgid,
)

internal fun fieldLabelRes(path: String): Int? = fieldLabels[path]
