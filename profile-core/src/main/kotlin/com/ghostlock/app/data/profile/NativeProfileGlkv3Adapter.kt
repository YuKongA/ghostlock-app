package com.ghostlock.app.data.profile

import com.ghostlock.app.data.NativeProfileDocument
import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.route.RouteKind

/**
 * NativeProfileDocument (v2 logical model) -> Glkv3Document path/type adapter
 * (GLKv3-3).
 *
 * The v2 writer ([NativeProfileDocument.toBinary]) remains the reader-compat
 * and test codec; this adapter is the production logical-document translation
 * used by `exportKernelProfiles` and the app native-document path (GLKv3-4). It
 * reuses the very
 * same [NativeProfileDocument.sections] emission the v2 writer uses, so the two
 * formats cannot list different fields, and it applies the native GLKv3 wire
 * types declared in `backend/cve_2026_43499/glkv3_schema.hpp` and
 * `backend/cve_2026_43284/glkv3_schema.hpp`:
 *
 *   - boolean flags   -> [Glkv3Value.Bool]   (safe_mode / vr_guard / compact_waiter);
 *   - signed geometry -> [Glkv3Value.Int]    (payload_delta / waiter_shift / waiter_off);
 *   - every other field -> [Glkv3Value.UInt].
 *
 * [declaredTypes] is the Kotlin leg of the path -> type manifest;
 * ProfileManifestV3AgreementTest asserts it equals the native-exported
 * `profile-manifest-v3.tsv`. The root selection keys are tokens
 * (release / terminal / backend / route), as the GLKv3 root map declares.
 */
object NativeProfileGlkv3Adapter {
    /** GLKv3 wire type labels; names match native `glkv3::wire_type_name`. */
    enum class WireType(val manifestName: String) {
        UInt("uint"),
        Int("int"),
        Bool("bool"),
        Str("str"),
        Bin("bin"),
        Array("array"),
    }

    /** v2 fixes the frontend to the root_child terminal (header `kTerminalRootChild`). */
    const val TERMINAL_ROOT_CHILD: String = "root_child"

    /**
     * Native path -> GLKv3 wire type. Kept byte-for-byte in step with the two
     * native GLKv3 FieldSpec lists; [declaredTypes] is asserted against the
     * native-exported manifest, so a one-sided change fails a test.
     */
    private val TYPE_BY_PATH: Map<String, WireType> = mapOf(
        "backend.cve_2026_43284.carrier_path" to WireType.UInt,
        "backend.cve_2026_43284.defex_symbol" to WireType.UInt,
        "backend.cve_2026_43284.kmi" to WireType.UInt,
        "backend.cve_2026_43284.late_load_args" to WireType.UInt,
        "backend.cve_2026_43284.lkm_path" to WireType.UInt,
        "backend.cve_2026_43284.selinux_exec_context" to WireType.UInt,
        "backend.cve_2026_43284.steps" to WireType.UInt,
        "backend.cve_2026_43499.steps" to WireType.UInt,
        "cred.caps_count" to WireType.UInt,
        "cred.caps_offset" to WireType.UInt,
        "cred.caps_value" to WireType.UInt,
        "cred.copy_size" to WireType.UInt,
        "cred.ref0_image" to WireType.UInt,
        "cred.ref0_offset" to WireType.UInt,
        "cred.ref1_image" to WireType.UInt,
        "cred.ref1_offset" to WireType.UInt,
        "cred.ref2_image" to WireType.UInt,
        "cred.ref2_offset" to WireType.UInt,
        "cred.ref3_image" to WireType.UInt,
        "cred.ref3_offset" to WireType.UInt,
        "cred.ref_count" to WireType.UInt,
        "cred.usage_offset" to WireType.UInt,
        "cred.usage_value" to WireType.UInt,
        "execution.consumer.burst_calls" to WireType.UInt,
        "execution.consumer.max_calls" to WireType.UInt,
        "execution.handoff.enforce_poll_attempts" to WireType.UInt,
        "execution.handoff.enforce_poll_interval_ms" to WireType.UInt,
        "execution.handoff.module_poll_attempts" to WireType.UInt,
        "execution.handoff.module_poll_interval_ms" to WireType.UInt,
        "execution.handoff.pre_dispatch_settle_ms" to WireType.UInt,
        "execution.heap.kernelsnitch_timeout_ms" to WireType.UInt,
        "execution.heap.prepare_max_attempts" to WireType.UInt,
        "execution.heap.prepare_timeout_ms" to WireType.UInt,
        "execution.race.route_done_timeout_ms" to WireType.UInt,
        "execution.race.route_wait_ms" to WireType.UInt,
        "execution.race.setup_settle_us" to WireType.UInt,
        "execution.race.state_poll_interval_us" to WireType.UInt,
        "execution.recommended_cpus.consumer" to WireType.UInt,
        "execution.recommended_cpus.main" to WireType.UInt,
        "execution.stages.w1_attempts" to WireType.UInt,
        "execution.stages.w1_scratch_repair_attempts" to WireType.UInt,
        "execution.stages.w1_settle_us" to WireType.UInt,
        "execution.stages.w2_attempts" to WireType.UInt,
        "execution.stages.w2_settle_us" to WireType.UInt,
        "execution.stages.w3_attempts" to WireType.UInt,
        "execution.stages.w3_chain_rounds" to WireType.UInt,
        "execution.stages.w3_settle_us" to WireType.UInt,
        "kernel.compact_waiter" to WireType.Bool,
        "kernel.kernel_phys_load" to WireType.UInt,
        "kernel.kernel_phys_offset" to WireType.UInt,
        "kernel.kernelsnitch_collisions" to WireType.UInt,
        "kernel.mm_struct_sz" to WireType.UInt,
        "meta.fallback_route" to WireType.UInt,
        "meta.kernel_major" to WireType.UInt,
        "meta.safe_mode" to WireType.Bool,
        "meta.vr_guard" to WireType.Bool,
        "offset.empty_zero_page" to WireType.UInt,
        "offset.init_cred" to WireType.UInt,
        "offset.init_task" to WireType.UInt,
        "offset.root_task_group" to WireType.UInt,
        "offset.security_hook_heads" to WireType.UInt,
        "offset.selinux_blob_sizes" to WireType.UInt,
        "offset.selinux_enforcing" to WireType.UInt,
        "offset.slide_boot_id" to WireType.UInt,
        "offset.slide_loggers_0_1" to WireType.UInt,
        "offset.slide_nfulnl_logger" to WireType.UInt,
        "offset.vr_sys_exit_tp" to WireType.UInt,
        "route.multicast_waiter.arm_hold" to WireType.UInt,
        "route.multicast_waiter.arm_sequence" to WireType.UInt,
        "route.multicast_waiter.attempts" to WireType.UInt,
        "route.multicast_waiter.buffer_size" to WireType.UInt,
        "route.multicast_waiter.lock_offset" to WireType.UInt,
        "route.multicast_waiter.task_offset" to WireType.UInt,
        "route.multicast_waiter.waiter_off" to WireType.Int,
        "route.select_stack.compact_waiter" to WireType.Bool,
        "route.select_stack.enter_delay_us" to WireType.UInt,
        "route.select_stack.timeout_us" to WireType.UInt,
        "route.select_stack.waiter_shift" to WireType.Int,
        "route.tcp_zerocopy.arm_sequence" to WireType.UInt,
        "route.tcp_zerocopy.attempts" to WireType.UInt,
        "route.tcp_zerocopy.chunk_bias" to WireType.UInt,
        "route.tcp_zerocopy.cred_copy_off" to WireType.UInt,
        "route.tcp_zerocopy.fake_task_off" to WireType.UInt,
        "route.tcp_zerocopy.payload_delta" to WireType.Int,
        "route.tcp_zerocopy.post_receive_hold_iterations" to WireType.UInt,
        "task_struct.atomic_flags" to WireType.UInt,
        "task_struct.comm" to WireType.UInt,
        "task_struct.cred" to WireType.UInt,
        "task_struct.normal_prio" to WireType.UInt,
        "task_struct.pi_blocked_on" to WireType.UInt,
        "task_struct.pi_lock" to WireType.UInt,
        "task_struct.pi_top_task" to WireType.UInt,
        "task_struct.pi_waiters" to WireType.UInt,
        "task_struct.pid" to WireType.UInt,
        "task_struct.prio" to WireType.UInt,
        "task_struct.real_cred" to WireType.UInt,
        "task_struct.sched_task_group" to WireType.UInt,
        "task_struct.seccomp" to WireType.UInt,
        "task_struct.tasks" to WireType.UInt,
        "task_struct.tgid" to WireType.UInt,
        "vr_guard.tracepoint_funcs" to WireType.UInt,
    )

    /** Every (path, wire) pair this adapter can emit, for manifest agreement. */
    fun declaredTypes(): Map<String, WireType> = TYPE_BY_PATH

    /** Translates a v2 logical document into the canonical GLKv3 logical document. */
    fun adapt(document: NativeProfileDocument): Glkv3Document {
        val sections = document.sections().map { section ->
            Glkv3Section(
                name = section.name,
                entries = section.entries.map { (key, raw) ->
                    val path = "${section.name}.$key"
                    val type = TYPE_BY_PATH[path]
                        ?: error("NativeProfileDocument emitted an unmapped GLKv3 path: $path")
                    Glkv3Entry(key, toValue(type, raw))
                },
            )
        }
        return Glkv3Document(
            release = document.release,
            terminal = TERMINAL_ROOT_CHILD,
            /* 43284 documents are route-less (native kRouteAuto); fromWire(0) is
             * null, so the root "route" key is omitted, matching v2. */
            backend = BackendKind.fromWire(document.backendKind.toInt())?.token,
            route = RouteKind.fromWire(document.routeKind)?.token,
            sections = sections,
        )
    }

    private fun toValue(type: WireType, raw: ULong): Glkv3Value = when (type) {
        WireType.UInt -> Glkv3Value.UInt(raw)
        WireType.Int -> Glkv3Value.Int(raw.toLong())
        WireType.Bool -> Glkv3Value.Bool(raw != 0uL)
        /* No current profile section carries text/binary/array values: the v2
         * wire has a single 64-bit slot per key. */
        WireType.Str, WireType.Bin, WireType.Array ->
            error("GLKv3 path is not a numeric or boolean section field")
    }
}
