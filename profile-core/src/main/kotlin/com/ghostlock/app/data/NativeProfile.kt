package com.ghostlock.app.data

import com.ghostlock.app.data.component.BackendKind
import com.ghostlock.app.data.component.CombinationCatalog
import com.ghostlock.app.data.component.CombinationSpec
import com.ghostlock.app.data.plugin.PluginEmission
import com.ghostlock.app.data.plugin.PluginValue
import com.ghostlock.app.data.profile.Glkv3Decoder
import com.ghostlock.app.data.route.NoRouteConfig
import com.ghostlock.app.data.route.RouteConfig
import com.ghostlock.app.data.route.RouteKind
import com.ghostlock.app.data.route.cve_2026_43499.Cve2026_43499RouteSections

/**
 * Logical (owner-agnostic) profile document: the resolved values and the
 * component selection, independent of any wire codec.
 *
 * It is produced by [from] (resolved values by dotted path) and consumed by
 * [com.ghostlock.app.data.profile.NativeProfileGlkv3Adapter], which translates
 * it to the canonical GLKv3 logical document;
 * [com.ghostlock.app.data.profile.Glkv3Encoder] then writes the MessagePack
 * bytes native reads (GLKv3-4). There is no v2 wire codec any more
 * (S4 R2c-2): presence is still carried by key occurrence and values are never
 * clamped, and only the active route's tuning is meaningful.
 */
data class NativeProfileDocument(
    val release: String,
    val routeKind: UInt,
    val kernelMajor: UInt,
    /** Optional kernel minor (HOCON refactor); unused today, carried for the UI. */
    val kernelMinor: UInt? = null,
    val taskStruct: TaskStructOffsets,
    val cred: CredTemplate,
    val kernelOffset: KernelOffsetTable,
    val kernelPhysLoad: ULong?,
    val kernelPhysOffset: ULong?,
    val compactWaiter: UByte?,
    val kernelsnitchCollisions: UInt?,
    val mmStructSz: UInt?,
    val execution: ExecutionTuning,
    val safeMode: UInt,
    /** Route-specific configuration; never part of the shared schema. */
    val routeConfig: RouteConfig,
    /**
     * S4 R6b combination token selection (ADR-0006 T5). Carried as a string at
     * backend.<id>.steps; it derives the route, step set and terminal. Null =
     * no token: the native selection rejects a missing token instead of
     * defaulting. [StepSetKind] survives only as the derived vocabulary.
     */
    val combination: CombinationSpec? = null,
    /**
     * M2 queue carrying (design doc 4.5 / 5-Q2): the queue-level route token,
     * the step queue and the static experimental opt-in declared by the
     * `available.<id>` object form. Null means "not declared": no key is
     * written, so every pre-M2 document keeps byte-identical output.
     *
     * The queue-level route is carried in the canonical map as
     * `backend.<id>.queue_route` (ProfileLayout.QueueRouteKey) because the route
     * geometry map owns `backend.<id>.route` there; [v3Sections] is the ONE
     * place that maps it back onto the wire key `route`.
     */
    val queueRoute: String? = null,
    val stepQueue: List<QueueElement>? = null,
    val experimental: Boolean? = null,
    /**
     * Backend id carried in the logical document (native `kBackend*`). Defaults
     * to cve_2026_43499. [BackendWireCve202643284] selects the 43284 private
     * section; every existing caller keeps the 43499 default and byte output.
     */
    val backendKind: UInt = BackendWireCve202643499,
    /**
     * cve_2026_43284 private policy (S3 B4); only written or read when
     * [backendKind] is the 43284 id. Null means "no 43284 section".
     */
    val cve2026_43284: Cve2026_43284Config? = null,
    /**
     * P1: ENABLED plugins only (see [PluginEmission.of]). Empty means the
     * document carries no `plugin.*` section at all, so every existing caller
     * keeps byte-identical output.
     */
    val plugins: List<PluginEmission> = emptyList(),
) {
    internal fun sections(): List<Section> = buildList {
        /* The kernel scalars are ROOT values on the wire (native kRootSection),
         * not a section; v3Sections drops this logical section and the adapter
         * reads the fields directly. */
        add(
            Section(
                "meta",
                listOf("kernel_major" to kernelMajor.toULong(), "safe_mode" to safeMode.toULong()) +
                    listOfNotNull(kernelMinor?.let { "kernel_minor" to it.toULong() }),
            ),
        )
        add(Section("task_struct", taskEntries()))
        add(Section("cred", credEntries()))
        add(Section("offset", offsetEntries()))
        kernelSection()?.let(::add)
        add(
            Section(
                "execution.recommended_cpus",
                listOf(
                    "main" to execution.recommendedMainCpu.toULong(),
                    "consumer" to execution.recommendedConsumerCpu.toULong(),
                ),
            ),
        )
        add(
            Section(
                "execution.heap",
                listOf(
                    "prepare_max_attempts" to execution.heapPrepareMaxAttempts.toULong(),
                    "prepare_timeout_ms" to execution.heapPrepareTimeoutMs.toULong(),
                    "kernelsnitch_timeout_ms" to execution.heapKernelsnitchTimeoutMs.toULong(),
                ),
            ),
        )
        add(
            Section(
                "execution.race",
                listOf(
                    "route_wait_ms" to execution.raceRouteWaitMs.toULong(),
                    "route_done_timeout_ms" to execution.raceRouteDoneTimeoutMs.toULong(),
                    "setup_settle_us" to execution.raceSetupSettleUs.toULong(),
                    "state_poll_interval_us" to execution.raceStatePollIntervalUs.toULong(),
                ),
            ),
        )
        add(
            Section(
                "execution.stages",
                listOf(
                    "w1_attempts" to execution.w1Attempts.toULong(),
                    "w1_settle_us" to execution.w1SettleUs.toULong(),
                    "w1_scratch_repair_attempts" to execution.w1ScratchRepairAttempts.toULong(),
                    "w2_attempts" to execution.w2Attempts.toULong(),
                    "w2_settle_us" to execution.w2SettleUs.toULong(),
                    "w3_chain_rounds" to execution.w3ChainRounds.toULong(),
                    "w3_attempts" to execution.w3Attempts.toULong(),
                    "w3_settle_us" to execution.w3SettleUs.toULong(),
                ),
            ),
        )
        add(
            Section(
                "execution.handoff",
                listOf(
                    "pre_dispatch_settle_ms" to execution.handoffPreDispatchSettleMs.toULong(),
                    "module_poll_attempts" to execution.handoffModulePollAttempts.toULong(),
                    "module_poll_interval_ms" to execution.handoffModulePollIntervalMs.toULong(),
                    "enforce_poll_attempts" to execution.handoffEnforcePollAttempts.toULong(),
                    "enforce_poll_interval_ms" to execution.handoffEnforcePollIntervalMs.toULong(),
                ),
            ),
        )
        add(
            Section(
                "execution.consumer",
                listOf(
                    "max_calls" to execution.consumerMaxCalls.toULong(),
                    "burst_calls" to execution.consumerBurstCalls.toULong(),
                ),
            ),
        )
        routeSection()?.let(::add)
        backendSection()?.let(::add)
        addAll(backend43284Sections())
    }

    /**
     * S4 R2 production GLKv3 sections: owner-qualified and selection aware.
     * Only the sections the document's selection owns plus the public
     * `common` section are emitted, so a 43284 document no longer carries the
     * 43499/platform sections. [sections] keeps the pre-owner logical names;
     * this view normalises them onto the owner-qualified wire names.
     */
    internal fun v3Sections(): List<Section> {
        val is43284 = backendKind == BackendWireCve202643284
        val out = mutableListOf<Section>()
        for (section in sections()) {
            when (section.name) {
                /* ROOT scalars: carried by the document, never a section. */
                "meta" -> Unit
                "task_struct" ->
                    if (!is43284) out += Section("backend.cve_2026_43499.abi.task_struct", section.entries)

                "cred" -> if (!is43284) {
                    val (platform, backend) = section.entries.partition { it.first in AbiCredKeys }
                    out += Section("backend.cve_2026_43499.abi.cred", platform)
                    out += Section("backend.cve_2026_43499.cred", backend)
                }

                "offset" -> if (!is43284) {
                    val (platform, backend) = section.entries.partition { it.first in AbiOffsetKeys }
                    out += Section("backend.cve_2026_43499.abi.offset", platform)
                    out += Section("backend.cve_2026_43499.offset", backend)
                }

                "kernel" -> if (!is43284) {
                    val (platform, backend) = section.entries.partition { it.first in AbiKernelKeys }
                    if (platform.isNotEmpty()) {
                        out += Section("backend.cve_2026_43499.abi.kernel", platform)
                    }
                    if (backend.isNotEmpty()) out += Section("backend.cve_2026_43499.kernel", backend)
                }

                "backend.cve_2026_43499", "backend.cve_2026_43284" -> out += section

                else -> {
                    /* 43284 sections are already owner-qualified (the execution
                     * tuning nests under it), so pass them through untouched. */
                    if (section.name.startsWith("backend.cve_2026_43284.")) {
                        if (is43284) out += section
                        continue
                    }
                    if (is43284) continue
                    val name = when {
                        section.name.startsWith("execution.") ->
                            "backend.cve_2026_43499." + section.name
                        // Route sections are owned by the 43499 backend (R2).
                        section.name.startsWith("route.") ->
                            Cve2026_43499RouteSections.sectionNameFor(
                                section.name.removePrefix("route."),
                            )
                        else -> null
                    } ?: continue
                    out += Section(name, section.entries)
                }
            }
        }
        if (plugins.isNotEmpty()) {
            /* Canonical plugin shape (contract-design 3.14.7.5, plugin/schema.hpp,
             * plugin/wire.cpp): ONE section named `plugin`, every key spelled
             * `<id>.<field>` — the id may itself contain dots. The static fields
             * are declared `plugin.<id>.{enabled,stage,module_path,module_hash}`
             * (enabled is a bool) and the parameter values are typed by the
             * descriptor. `extract.*` is NOT emitted here: it is the extractor
             * projection, not an App runtime setting. */
            val static = mutableListOf<Pair<String, String>>()
            val dynamic = mutableListOf<Pair<String, PluginValue>>()
            for (plugin in plugins) {
                dynamic += plugin.id + ".enabled" to PluginValue.Bool(true)
                plugin.stage?.let { static += plugin.id + ".stage" to it }
                static += plugin.id + ".module_path" to plugin.modulePath
                static += plugin.id + ".module_hash" to plugin.moduleHash
                for (param in plugin.params) {
                    dynamic += plugin.id + ".params." + param.name to param.value
                }
                /* P2: extractor values ride the same section, keyed
                 * `<id>.extract.<key>`; only resolved keys are present. */
                for (entry in plugin.extract) {
                    dynamic += plugin.id + ".extract." + entry.name to entry.value
                }
            }
            out += Section(
                name = "plugin",
                entries = emptyList(),
                textEntries = static,
                pluginEntries = dynamic,
            )
        }
        return out.filter {
            it.entries.isNotEmpty() || it.textEntries.isNotEmpty() || it.pluginEntries.isNotEmpty()
        }
    }

    /**
     * Backend-private combination token section (cve_2026_43499). M2 adds the
     * declared queue selection to it: the canonical `queue_route` token is
     * emitted under the wire key `route` — the ONE mapping point (the canonical
     * key exists only because the route geometry map owns `route` there), while
     * the queue geometry keeps riding `backend.cve_2026_43499.route.<branch>.*`.
     */
    private fun backendSection(): Section? =
        if (backendKind == BackendWireCve202643284) {
            null
        } else {
            val token = combination?.takeIf { it.backend == BackendKind.Cve2026_43499 }?.token
            val text = buildList {
                queueRoute?.let { add("route" to it) }
                token?.let { add("steps" to it) }
            }
            val entries = buildList {
                /* Presence is key occurrence: the bool is only written when the
                 * declaration exists (U5 opt-in), never defaulted to false. */
                experimental?.let { add("experimental" to if (it) 1uL else 0uL) }
            }
            val queue = buildList { stepQueue?.let { add("queue" to it) } }
            if (entries.isEmpty() && text.isEmpty() && queue.isEmpty()) {
                null
            } else {
                Section("backend.cve_2026_43499", entries, text, queueEntries = queue)
            }
        }

    /**
     * cve_2026_43284 backend-private policy section (S3 B4). Written only when
     * the header backend id is 43284; an absent field stays absent (presence is
     * carried by key occurrence, so a provided 0 is distinct from omitted).
     */
    private fun backend43284Sections(): List<Section> {
        if (backendKind != BackendWireCve202643284) return emptyList()
        /* M2: this backend has no route axis, so a queue-level route is not
         * applicable (native `route-not-applicable`); refusing here keeps the
         * document from silently dropping a declared route. */
        require(queueRoute == null) {
            "backend.cve_2026_43284 has no route axis: queue route not applicable"
        }
        val config = cve2026_43284 ?: Cve2026_43284Config()
        /* kmi / lkm_path / carrier_path are native-side conventions now (the wire
         * keeps them; the profile must not provide them). */
        val text = buildList {
            combination?.takeIf { it.backend == BackendKind.Cve2026_43284 }?.let {
                add("steps" to it.token)
            }
        }
        /* HOCON refactor: the execution tuning moved under `execution.*`. */
        val entries = buildList {
            config.selinuxExecContext?.let { add("selinux_exec_context" to it) }
            config.lateLoadArgs?.let { add("late_load_args" to it) }
            config.waitTimeoutMs?.let { add("wait_timeout_ms" to it.toULong()) }
            config.modulePollAttempts?.let { add("module_poll_attempts" to it.toULong()) }
            config.modulePollIntervalMs?.let { add("module_poll_interval_ms" to it.toULong()) }
        }
        /* M2 queue selection (U5 experimental opt-in + step queue). */
        val selection = buildList {
            experimental?.let { add("experimental" to if (it) 1uL else 0uL) }
        }
        val queue = buildList { stepQueue?.let { add("queue" to it) } }
        val out = mutableListOf<Section>()
        if (text.isNotEmpty() || selection.isNotEmpty() || queue.isNotEmpty()) {
            out += Section("backend.cve_2026_43284", selection, text, queueEntries = queue)
        }
        if (entries.isNotEmpty()) out += Section("backend.cve_2026_43284.execution", entries)
        return out
    }

    private fun taskEntries(): List<Pair<String, ULong>> = listOf(
        "prio" to taskStruct.prio.toULong(),
        "normal_prio" to taskStruct.normalPrio.toULong(),
        "sched_task_group" to taskStruct.schedTaskGroup.toULong(),
        "pi_lock" to taskStruct.piLock.toULong(),
        "pi_waiters" to taskStruct.piWaiters.toULong(),
        "pi_top_task" to taskStruct.piTopTask.toULong(),
        "pi_blocked_on" to taskStruct.piBlockedOn.toULong(),
        "pid" to taskStruct.pid.toULong(),
        "tgid" to taskStruct.tgid.toULong(),
        "atomic_flags" to taskStruct.atomicFlags.toULong(),
        "real_cred" to taskStruct.realCred.toULong(),
        "cred" to taskStruct.cred.toULong(),
        "comm" to taskStruct.comm.toULong(),
        "tasks" to taskStruct.tasks.toULong(),
        "seccomp" to taskStruct.seccomp.toULong(),
    )

    private fun credEntries(): List<Pair<String, ULong>> = listOf(
        "copy_size" to cred.copySize.toULong(),
        "usage_offset" to cred.usageOffset.toULong(),
        "usage_value" to cred.usageValue.toULong(),
        "caps_offset" to cred.capsOffset.toULong(),
        "caps_count" to cred.capsCount.toULong(),
        "caps_value" to cred.capsValue,
        "ref_count" to cred.refCount.toULong(),
        "ref0_offset" to cred.ref0Offset.toULong(),
        "ref1_offset" to cred.ref1Offset.toULong(),
        "ref2_offset" to cred.ref2Offset.toULong(),
        "ref3_offset" to cred.ref3Offset.toULong(),
        "ref0_image" to cred.ref0Image,
        "ref1_image" to cred.ref1Image,
        "ref2_image" to cred.ref2Image,
        "ref3_image" to cred.ref3Image,
    )

    private fun offsetEntries(): List<Pair<String, ULong>> = buildList {
        addAll(baseOffsetEntries())
        /* Ancillary vr.ko guard: omitted when the profile does not carry it, so
         * profiles without the behavior keep byte-identical output. */
        kernelOffset.vrSysExitTp.takeIf { it != 0uL }?.let { add("vr_sys_exit_tp" to it) }
    }

    private fun baseOffsetEntries(): List<Pair<String, ULong>> = listOf(
        "init_task" to kernelOffset.initTask,
        "init_cred" to kernelOffset.initCred,
        "empty_zero_page" to kernelOffset.emptyZeroPage,
        "root_task_group" to kernelOffset.rootTaskGroup,
        "selinux_enforcing" to kernelOffset.selinuxEnforcing,
        "selinux_blob_sizes" to kernelOffset.selinuxBlobSizes,
        "security_hook_heads" to kernelOffset.securityHookHeads,
        "slide_nfulnl_logger" to kernelOffset.slideNfulnlLogger,
        "slide_loggers_0_1" to kernelOffset.slideLoggers01,
        "slide_boot_id" to kernelOffset.slideBootId,
    )

    private fun kernelSection(): Section? {
        val entries = buildList {
            kernelPhysLoad?.let { add("kernel_phys_load" to it) }
            kernelPhysOffset?.let { add("kernel_phys_offset" to it) }
            compactWaiter?.let { add("compact_waiter" to it.toULong()) }
            kernelsnitchCollisions?.let { add("kernelsnitch_collisions" to it.toULong()) }
            mmStructSz?.let { add("mm_struct_sz" to it.toULong()) }
        }
        return if (entries.isEmpty()) null else Section("kernel", entries)
    }

    private fun routeSection(): Section? {
        val kind = RouteKind.fromWire(routeKind) ?: return null
        val entries = routeConfig.entries()
        return if (entries.isEmpty()) null else Section(routeSectionName(kind.wire), entries)
    }

    companion object {
        fun routeKind(route: String?): UInt =
            RouteKind.resolve(RouteKind.normalize(route))?.wire ?: 0u

        /**
         * Returns a canonical copy of the GLKv3 [document] with `common.safe_mode`
         * set to true, or null when the blob is not a well-formed GLKv3 document.
         * All other root keys and sections are preserved (modulo canonical key
         * order). The v2 scanning fallback was removed with the v2 wire (S4 R2c).
         */
        fun patchSafeMode(document: ByteArray): ByteArray? =
            Glkv3Decoder.patchSafeMode(document)

        /** Builds the document from resolved profile values by dotted path. */
        fun from(
            release: String,
            route: String?,
            text: (String) -> String? = { null },
            bool: (String) -> Boolean? = { null },
            value: (String) -> Long?,
            /** P1: enabled plugins only; empty keeps every caller byte-identical. */
            plugins: List<PluginEmission> = emptyList(),
        ): NativeProfileDocument {
            fun vu(path: String): UInt = value(path)?.toUInt() ?: 0u
            fun vul(path: String): ULong = value(path)?.toULong() ?: 0uL
            fun vuOrNull(path: String): UInt? = value(path)?.toUInt()
            fun vulOrNull(path: String): ULong? = value(path)?.toULong()
            /* S4 R6b: the combination token at backend.steps is the single
             * selection source. It is resolved against the selected backend and
             * derives the route / step set / terminal. An unknown token fails
             * closed with the token text echoed. */
            val backendKind = BackendKind.resolve(BackendKind.normalize(text("backend.kind")))
                ?: BackendKind.Default
            val combinationToken = text("backend.steps")
            val combination = if (combinationToken == null) {
                null
            } else {
                /* Accept both the canonical combination token and a legacy step
                 * id (migrated through the declared route), mirroring the HOCON
                 * migration; an unknown value fails closed with its text. */
                CombinationCatalog.fromLegacySteps(
                    backendKind, combinationToken, RouteKind.resolve(RouteKind.normalize(route)),
                ) ?: throw IllegalArgumentException(
                    "backend.steps is not a known combination token for " +
                        "${backendKind.token}: $combinationToken",
                )
            }
            /* The token derives the route, so the geometry lookup follows it
             * rather than the independently declared profile route. */
            val effectiveRoute = combination?.route?.token ?: route
            val derivedRouteKind = combination?.let { it.route?.wire ?: 0u } ?: routeKind(route)
            /* Boolean HOCON flags prefer the bool accessor; the route branch is
             * consulted first (mirroring nativeValue), and a legacy numeric
             * spelling still decodes for imported v1 profiles. */
            fun flagAt(path: String): Boolean? {
                effectiveRoute?.let { name -> bool("route.$name.$path")?.let { return it } }
                return bool(path) ?: value(path)?.let { it != 0L }
            }
            val routeConfig = RouteKind.resolve(RouteKind.normalize(effectiveRoute))?.buildConfig(value)
                ?: NoRouteConfig
            /* S4 R4: the 43284 policy paths and handshake tuning ride the same
             * dotted-path accessors. Only a 43284 selection builds the private
             * config; an absent key stays absent (no default is injected here,
             * the native schema owns the defaults). */
            val config43284 = if (backendKind == BackendKind.Cve2026_43284) {
                fun textAt(path: String): String? =
                    text("backend.cve_2026_43284.$path")?.takeIf { it.isNotEmpty() }
                fun valueAt(path: String): Long? =
                    value("backend.cve_2026_43284.$path")
                Cve2026_43284Config(


                    selinuxExecContext = valueAt("selinux_exec_context")?.toULong(),
                    lateLoadArgs = valueAt("late_load_args")?.toULong(),
                    waitTimeoutMs = valueAt("wait_timeout_ms")?.toUInt(),
                    modulePollAttempts = valueAt("module_poll_attempts")?.toUInt(),
                    modulePollIntervalMs = valueAt("module_poll_interval_ms")?.toUInt(),
                )
            } else {
                null
            }
            return NativeProfileDocument(
                release = release,
                routeKind = derivedRouteKind,
                kernelMajor = vu("kernel_major"),
                kernelMinor = vuOrNull("kernel_minor"),
                taskStruct = TaskStructOffsets(
                    prio = vu("task_struct.prio"),
                    normalPrio = vu("task_struct.normal_prio"),
                    schedTaskGroup = vu("task_struct.sched_task_group"),
                    piLock = vu("task_struct.pi_lock"),
                    piWaiters = vu("task_struct.pi_waiters"),
                    piTopTask = vu("task_struct.pi_top_task"),
                    piBlockedOn = vu("task_struct.pi_blocked_on"),
                    pid = vu("task_struct.pid"),
                    tgid = vu("task_struct.tgid"),
                    atomicFlags = vu("task_struct.atomic_flags"),
                    realCred = vu("task_struct.real_cred"),
                    cred = vu("task_struct.cred"),
                    comm = vu("task_struct.comm"),
                    tasks = vu("task_struct.tasks"),
                    seccomp = vu("task_struct.seccomp"),
                ),
                cred = CredTemplate(
                    copySize = vu("cred.copy_size"),
                    usageOffset = vu("cred.usage_offset"),
                    usageValue = vu("cred.usage_value"),
                    capsOffset = vu("cred.caps_offset"),
                    capsCount = vu("cred.caps_count"),
                    capsValue = vul("cred.caps_value"),
                    refCount = vu("cred.ref_count"),
                    ref0Offset = vu("cred.ref0_offset"),
                    ref1Offset = vu("cred.ref1_offset"),
                    ref2Offset = vu("cred.ref2_offset"),
                    ref3Offset = vu("cred.ref3_offset"),
                    ref0Image = vul("cred.ref0_image"),
                    ref1Image = vul("cred.ref1_image"),
                    ref2Image = vul("cred.ref2_image"),
                    ref3Image = vul("cred.ref3_image"),
                ),
                kernelOffset = KernelOffsetTable(
                    initTask = vul("offset.init_task"),
                    initCred = vul("offset.init_cred"),
                    emptyZeroPage = vul("offset.empty_zero_page"),
                    rootTaskGroup = vul("offset.root_task_group"),
                    selinuxEnforcing = vul("offset.selinux_enforcing"),
                    selinuxBlobSizes = vul("offset.selinux_blob_sizes"),
                    securityHookHeads = vul("offset.security_hook_heads"),
                    slideNfulnlLogger = vul("offset.slide_nfulnl_logger"),
                    slideLoggers01 = vul("offset.slide_loggers_0_1"),
                    slideBootId = vul("offset.slide_boot_id"),
                    vrSysExitTp = vul("offset.vr_sys_exit_tp"),
                ),
                kernelPhysLoad = vulOrNull("kernel_phys_load"),
                kernelPhysOffset = vulOrNull("kernel_phys_offset"),
                compactWaiter = flagAt("compact_waiter")?.let { if (it) 1u.toUByte() else 0u.toUByte() },
                kernelsnitchCollisions = vuOrNull("kernelsnitch.collisions"),
                mmStructSz = vuOrNull("kernelsnitch.mm_struct_sz"),
                execution = ExecutionTuning(
                    recommendedMainCpu = vu("execution.recommended_cpus.main"),
                    recommendedConsumerCpu = vu("execution.recommended_cpus.consumer"),
                    heapPrepareMaxAttempts = vu("execution.heap.prepare_max_attempts"),
                    heapPrepareTimeoutMs = vu("execution.heap.prepare_timeout_ms"),
                    heapKernelsnitchTimeoutMs = vu("execution.heap.kernelsnitch_timeout_ms"),
                    raceRouteWaitMs = vu("execution.race.route_wait_ms"),
                    raceRouteDoneTimeoutMs = vu("execution.race.route_done_timeout_ms"),
                    raceSetupSettleUs = vu("execution.race.setup_settle_us"),
                    raceStatePollIntervalUs = vu("execution.race.state_poll_interval_us"),
                    w1Attempts = vu("execution.stages.w1_attempts"),
                    w1SettleUs = vu("execution.stages.w1_settle_us"),
                    w1ScratchRepairAttempts = vu("execution.stages.w1_scratch_repair_attempts"),
                    w2Attempts = vu("execution.stages.w2_attempts"),
                    w2SettleUs = vu("execution.stages.w2_settle_us"),
                    w3ChainRounds = vu("execution.stages.w3_chain_rounds"),
                    w3Attempts = vu("execution.stages.w3_attempts"),
                    w3SettleUs = vu("execution.stages.w3_settle_us"),
                    handoffPreDispatchSettleMs = vu("execution.handoff.pre_dispatch_settle_ms"),
                    handoffModulePollAttempts = vu("execution.handoff.module_poll_attempts"),
                    handoffModulePollIntervalMs = vu("execution.handoff.module_poll_interval_ms"),
                    handoffEnforcePollAttempts = vu("execution.handoff.enforce_poll_attempts"),
                    handoffEnforcePollIntervalMs = vu("execution.handoff.enforce_poll_interval_ms"),
                    consumerMaxCalls = vu("execution.routes.select_stack.consumer_max_calls"),
                    consumerBurstCalls = vu("execution.routes.select_stack.consumer_burst_calls"),
                ),
                safeMode = 0u,
                routeConfig = routeConfig,
                combination = combination,
                backendKind = backendKind.wire.toUInt(),
                cve2026_43284 = config43284,
                plugins = plugins,
            )
        }


    }
}

/**
 * cve_2026_43284 backend-private policy values (S3 B4 / stringified S4 R4).
 * carrier_path / lkm_path are UTF-8 paths (<=256 bytes, absolute path
 * recommended); the rest are numeric tuning. null means absent (presence is
 * carried by key occurrence, so an omitted field is not an empty string).
 */
data class Cve2026_43284Config(
    val selinuxExecContext: ULong? = null,
    val lateLoadArgs: ULong? = null,
    val waitTimeoutMs: UInt? = null,
    val modulePollAttempts: UInt? = null,
    val modulePollIntervalMs: UInt? = null,
)

/* Header backend ids (native `kBackend*`) derived from the App-side enum
 * authority, shared by the data class default and the v3 section builder. */
private val BackendWireCve202643499: UInt = BackendKind.Cve2026_43499.wire.toUInt()
private val BackendWireCve202643284: UInt = BackendKind.Cve2026_43284.wire.toUInt()

internal data class Section(
    val name: String,
    val entries: List<Pair<String, ULong>>,
    /** S4 R4: WireKind::String values, emitted as GLKv3 str (<=256 UTF-8 bytes). */
    val textEntries: List<Pair<String, String>> = emptyList(),
    /**
     * P1: dynamically typed plugin parameters. Their concrete kind comes from
     * the plugin descriptor, so the manifest declares them as a union and the
     * adapter checks membership instead of picking a fixed kind.
     */
    val pluginEntries: List<Pair<String, PluginValue>> = emptyList(),
    /**
     * M2: composite values — the step queue is an array of maps. The manifest
     * declares the path `array`; the adapter checks that kind before emitting.
     */
    val queueEntries: List<Pair<String, List<QueueElement>>> = emptyList(),
)

/**
 * M2 one step-queue element (design 5-Q1/5-Q3): exactly one of `step`/`seam`,
 * and `stage` (a reserved plugin-stage identifier) only rides with `seam`. The
 * declaration shape is validated by
 * [com.ghostlock.app.data.ProfileLayout]; the wire shape is a map of str.
 */
data class QueueElement(
    val step: String? = null,
    val seam: String? = null,
    val stage: String? = null,
)

/** Split of the legacy `cred` logical section: ABI offsets (43499.abi.cred). */
private val AbiCredKeys = setOf(
    "usage_offset", "caps_offset", "ref_count",
    "ref0_offset", "ref1_offset", "ref2_offset", "ref3_offset",
)

/** Split of the legacy `offset` logical section: ABI symbols (43499.abi.offset). */
private val AbiOffsetKeys = setOf(
    "init_task", "init_cred", "empty_zero_page", "root_task_group",
    "selinux_enforcing", "selinux_blob_sizes", "security_hook_heads",
)

/** Split of the legacy `kernel` logical section: ABI phys facts (43499.abi.kernel). */
private val AbiKernelKeys = setOf("kernel_phys_load", "kernel_phys_offset")

private fun routeSectionName(route: UInt): String = when (RouteKind.fromWire(route)) {
    RouteKind.TCP_ZEROCOPY -> "route.tcp_zerocopy"
    RouteKind.SELECT_STACK -> "route.select_stack"
    RouteKind.MULTICAST_WAITER -> "route.multicast_waiter"
    null -> ""
}

data class TaskStructOffsets(
    val prio: UInt = 0u,
    val normalPrio: UInt = 0u,
    val schedTaskGroup: UInt = 0u,
    val piLock: UInt = 0u,
    val piWaiters: UInt = 0u,
    val piTopTask: UInt = 0u,
    val piBlockedOn: UInt = 0u,
    val pid: UInt = 0u,
    val tgid: UInt = 0u,
    val atomicFlags: UInt = 0u,
    val realCred: UInt = 0u,
    val cred: UInt = 0u,
    val comm: UInt = 0u,
    val tasks: UInt = 0u,
    val seccomp: UInt = 0u,
)

data class CredTemplate(
    val copySize: UInt = 0u,
    val usageOffset: UInt = 0u,
    val usageValue: UInt = 0u,
    val capsOffset: UInt = 0u,
    val capsCount: UInt = 0u,
    val capsValue: ULong = 0uL,
    val refCount: UInt = 0u,
    val ref0Offset: UInt = 0u,
    val ref1Offset: UInt = 0u,
    val ref2Offset: UInt = 0u,
    val ref3Offset: UInt = 0u,
    val ref0Image: ULong = 0uL,
    val ref1Image: ULong = 0uL,
    val ref2Image: ULong = 0uL,
    val ref3Image: ULong = 0uL,
)

data class KernelOffsetTable(
    val initTask: ULong = 0uL,
    val initCred: ULong = 0uL,
    val emptyZeroPage: ULong = 0uL,
    val rootTaskGroup: ULong = 0uL,
    val selinuxEnforcing: ULong = 0uL,
    val selinuxBlobSizes: ULong = 0uL,
    val securityHookHeads: ULong = 0uL,
    val slideNfulnlLogger: ULong = 0uL,
    val slideLoggers01: ULong = 0uL,
    val slideBootId: ULong = 0uL,
    /** Image offset of __tracepoint_sys_exit (ancillary vr.ko guard). */
    val vrSysExitTp: ULong = 0uL,
)

data class ExecutionTuning(
    val recommendedMainCpu: UInt = 0u,
    val recommendedConsumerCpu: UInt = 0u,
    val heapPrepareMaxAttempts: UInt = 0u,
    val heapPrepareTimeoutMs: UInt = 0u,
    val heapKernelsnitchTimeoutMs: UInt = 0u,
    val raceRouteWaitMs: UInt = 0u,
    val raceRouteDoneTimeoutMs: UInt = 0u,
    val raceSetupSettleUs: UInt = 0u,
    val raceStatePollIntervalUs: UInt = 0u,
    val w1Attempts: UInt = 0u,
    val w1SettleUs: UInt = 0u,
    val w1ScratchRepairAttempts: UInt = 0u,
    val w2Attempts: UInt = 0u,
    val w2SettleUs: UInt = 0u,
    val w3ChainRounds: UInt = 0u,
    val w3Attempts: UInt = 0u,
    val w3SettleUs: UInt = 0u,
    val handoffPreDispatchSettleMs: UInt = 0u,
    val handoffModulePollAttempts: UInt = 0u,
    val handoffModulePollIntervalMs: UInt = 0u,
    val handoffEnforcePollAttempts: UInt = 0u,
    val handoffEnforcePollIntervalMs: UInt = 0u,
    val consumerMaxCalls: UInt = 0u,
    val consumerBurstCalls: UInt = 0u,
)
