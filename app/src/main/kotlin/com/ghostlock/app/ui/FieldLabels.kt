package com.ghostlock.app.ui

import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import com.ghostlock.app.R

/** Localized display name for a field path, falling back to the key. */
@Composable
internal fun fieldLabel(path: String, fallback: String): String =
    fieldLabelRes(path)?.let { stringResource(it) } ?: fallback

private fun fieldLabelRes(path: String): Int? = when (path) {
        "route.multicast_waiter.compact_waiter" -> R.string.field_compact_waiter
        "fallback.to" -> R.string.field_fallback_to
        "route.tcp_zerocopy.compact_waiter" -> R.string.field_compact_waiter
        "fallback.route.tcp_zerocopy.compact_waiter" -> R.string.field_compact_waiter
        "cred.caps_count" -> R.string.field_cred_caps_count
        "cred.caps_offset" -> R.string.field_cred_caps_offset
        "cred.caps_value" -> R.string.field_cred_caps_value
        "cred.copy_size" -> R.string.field_cred_copy_size
        "cred.ref0_image" -> R.string.field_cred_ref0_image
        "cred.ref0_offset" -> R.string.field_cred_ref0_offset
        "cred.ref1_image" -> R.string.field_cred_ref1_image
        "cred.ref1_offset" -> R.string.field_cred_ref1_offset
        "cred.ref2_image" -> R.string.field_cred_ref2_image
        "cred.ref2_offset" -> R.string.field_cred_ref2_offset
        "cred.ref3_image" -> R.string.field_cred_ref3_image
        "cred.ref3_offset" -> R.string.field_cred_ref3_offset
        "cred.ref_count" -> R.string.field_cred_ref_count
        "cred.usage_value" -> R.string.field_cred_usage_value
        "execution.handoff.enforce_poll_attempts" -> R.string.field_execution_handoff_enforce_poll_attempts
        "execution.handoff.enforce_poll_interval_ms" -> R.string.field_execution_handoff_enforce_poll_interval_ms
        "execution.handoff.module_poll_attempts" -> R.string.field_execution_handoff_module_poll_attempts
        "execution.handoff.module_poll_interval_ms" -> R.string.field_execution_handoff_module_poll_interval_ms
        "execution.handoff.pre_dispatch_settle_ms" -> R.string.field_execution_handoff_pre_dispatch_settle_ms
        "execution.heap.kernelsnitch_timeout_ms" -> R.string.field_execution_heap_kernelsnitch_timeout_ms
        "execution.heap.prepare_max_attempts" -> R.string.field_execution_heap_prepare_max_attempts
        "execution.heap.prepare_timeout_ms" -> R.string.field_execution_heap_prepare_timeout_ms
        "execution.race.route_wait_ms" -> R.string.field_execution_race_route_wait_ms
        "execution.race.setup_settle_us" -> R.string.field_execution_race_setup_settle_us
        "execution.race.state_poll_interval_us" -> R.string.field_execution_race_state_poll_interval_us
        "execution.recommended_cpus.consumer" -> R.string.field_execution_recommended_cpus_consumer
        "execution.recommended_cpus.main" -> R.string.field_execution_recommended_cpus_main
        "execution.routes.multicast_waiter.post_adjust_settle_us" -> R.string.field_execution_routes_multicast_waiter_post_adjust_settle_us
        "execution.routes.multicast_waiter.post_requeue_settle_us" -> R.string.field_execution_routes_multicast_waiter_post_requeue_settle_us
        "execution.routes.multicast_waiter.ready_timeout_ms" -> R.string.field_execution_routes_multicast_waiter_ready_timeout_ms
        "execution.routes.select_stack.consumer_burst_calls" -> R.string.field_execution_routes_select_stack_consumer_burst_calls
        "execution.routes.select_stack.consumer_max_calls" -> R.string.field_execution_routes_select_stack_consumer_max_calls
        "execution.routes.select_stack.enter_delay_us" -> R.string.field_execution_routes_select_stack_enter_delay_us
        "execution.routes.select_stack.timeout_us" -> R.string.field_execution_routes_select_stack_timeout_us
        "execution.routes.tcp_zerocopy.arm_sequence" -> R.string.field_execution_routes_tcp_zerocopy_arm_sequence
        "execution.routes.tcp_zerocopy.attempts" -> R.string.field_execution_routes_tcp_zerocopy_attempts
        "execution.routes.tcp_zerocopy.post_receive_hold_iterations" -> R.string.field_execution_routes_tcp_zerocopy_post_receive_hold_iterations
        "execution.selected_cpus.consumer" -> R.string.field_execution_selected_cpus_consumer
        "execution.selected_cpus.main" -> R.string.field_execution_selected_cpus_main
        "execution.stages.w1_attempts" -> R.string.field_execution_stages_w1_attempts
        "execution.stages.w1_scratch_repair_attempts" -> R.string.field_execution_stages_w1_scratch_repair_attempts
        "execution.stages.w1_settle_us" -> R.string.field_execution_stages_w1_settle_us
        "execution.stages.w2_attempts" -> R.string.field_execution_stages_w2_attempts
        "execution.stages.w2_settle_us" -> R.string.field_execution_stages_w2_settle_us
        "execution.stages.w3_attempts" -> R.string.field_execution_stages_w3_attempts
        "execution.stages.w3_chain_rounds" -> R.string.field_execution_stages_w3_chain_rounds
        "execution.stages.w3_settle_us" -> R.string.field_execution_stages_w3_settle_us
        "kernel_major" -> R.string.field_kernel_major
        "kernelsnitch.collisions" -> R.string.field_kernelsnitch_collisions
        "kernelsnitch.mm_struct_sz" -> R.string.field_mm_struct_sz
        "kernel_phys_load" -> R.string.field_kernel_phys_load
        "route.multicast_waiter.buffer_size" -> R.string.field_mcast_buffer_size
        "fallback.route.multicast_waiter.buffer_size" -> R.string.field_mcast_buffer_size
        "route.multicast_waiter.fake_lock_offset" -> R.string.field_mcast_fake_lock_offset
        "fallback.route.multicast_waiter.fake_lock_offset" -> R.string.field_mcast_fake_lock_offset
        "route.multicast_waiter.fake_task_offset" -> R.string.field_mcast_fake_task_offset
        "fallback.route.multicast_waiter.fake_task_offset" -> R.string.field_mcast_fake_task_offset
        "route.multicast_waiter.lock_offset" -> R.string.field_mcast_lock_offset
        "fallback.route.multicast_waiter.lock_offset" -> R.string.field_mcast_lock_offset
        "route.multicast_waiter.lock_slot_count" -> R.string.field_mcast_lock_slot_count
        "fallback.route.multicast_waiter.lock_slot_count" -> R.string.field_mcast_lock_slot_count
        "route.multicast_waiter.lock_slot_stride" -> R.string.field_mcast_lock_slot_stride
        "fallback.route.multicast_waiter.lock_slot_stride" -> R.string.field_mcast_lock_slot_stride
        "route.multicast_waiter.lock_slots_offset" -> R.string.field_mcast_lock_slots_offset
        "fallback.route.multicast_waiter.lock_slots_offset" -> R.string.field_mcast_lock_slots_offset
        "route.multicast_waiter.task_offset" -> R.string.field_mcast_task_offset
        "fallback.route.multicast_waiter.task_offset" -> R.string.field_mcast_task_offset
        "route.multicast_waiter.waiter_off" -> R.string.field_mcast_waiter_off
        "fallback.route.multicast_waiter.waiter_off" -> R.string.field_mcast_waiter_off
        "offset.empty_zero_page" -> R.string.field_off_empty_zero_page
        "offset.init_cred" -> R.string.field_off_init_cred
        "offset.init_task" -> R.string.field_off_init_task
        "offset.mcast_fake_bss" -> R.string.field_off_mcast_fake_bss
        "offset.root_task_group" -> R.string.field_off_root_task_group
        "offset.security_hook_heads" -> R.string.field_off_security_hook_heads
        "offset.selinux_blob_sizes" -> R.string.field_off_selinux_blob_sizes
        "offset.selinux_enforcing" -> R.string.field_off_selinux_enforcing
        "offset.slide_boot_id" -> R.string.field_off_slide_boot_id
        "offset.slide_loggers_0_1" -> R.string.field_off_slide_loggers_0_1
        "offset.slide_nfulnl_logger" -> R.string.field_off_slide_nfulnl_logger
        "route.select_stack.waiter_shift" -> R.string.field_pselect_waiter_shift
        "fallback.route.select_stack.waiter_shift" -> R.string.field_pselect_waiter_shift
        "task_struct.atomic_flags" -> R.string.field_task_atomic_flags
        "task_struct.comm" -> R.string.field_task_comm
        "task_struct.cred" -> R.string.field_task_cred
        "task_struct.normal_prio" -> R.string.field_task_normal_prio
        "task_struct.pi_blocked_on" -> R.string.field_task_pi_blocked_on
        "task_struct.pi_lock" -> R.string.field_task_pi_lock
        "task_struct.pi_top_task" -> R.string.field_task_pi_top_task
        "task_struct.pi_waiters" -> R.string.field_task_pi_waiters
        "task_struct.pid" -> R.string.field_task_pid
        "task_struct.prio" -> R.string.field_task_prio
        "task_struct.real_cred" -> R.string.field_task_real_cred
        "task_struct.sched_task_group" -> R.string.field_task_sched_task_group
        "task_struct.seccomp" -> R.string.field_task_seccomp
        "task_struct.tasks" -> R.string.field_task_tasks
        "task_struct.tgid" -> R.string.field_task_tgid
    else -> null
}
