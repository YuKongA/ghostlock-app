package com.ghostlock.app.backend.backends.cve43499.perf

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.data.cve43499.A301SoDebugConfig
import com.ghostlock.app.jni.Native43499CloseResult
import com.ghostlock.app.jni.Native43499PerfCollectResult
import com.ghostlock.app.jni.Native43499PerfOpenRequest
import com.ghostlock.app.jni.Native43499PerfOpenResult
import java.io.File
import java.security.MessageDigest
import java.util.concurrent.TimeUnit
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Test
import org.junit.runner.RunWith

/**
 * G1 真机安全 perf 机制探针（androidTest，仅 debug 测试链）。
 *
 * 只做：spawn 同 app 子进程 → `Cve43499PerfNative` open/collect/close 采样 →
 * 解析/统计 ABI 分布；另加 open 诊断矩阵（self/child × 含/不含 kernel）定位
 * `perf_event_open` 权限失败来源。**不写内核/不落页/不进入 Flow/攻击路径**。
 *
 * 临时启用：debug asset 全程保持 `perf.enabled=0`；测试仅内存 `copy(enabled=1)`
 * 取得参数并直接调用机制入口（不改 asset）。结束断言 asset 仍 `enabled = 0`
 * 且 SHA-256 与开始时一致 = 恢复证据。
 *
 * 期望：至少一个 ABI=1 的 280B record（262145=IP|REGS_INTR 投递寄存器）；
 * 跨任务 open 失败则记录 errno/诊断矩阵并 fail（不崩）。
 *
 * @note 设备：A301SO（QV770MFGJ1）；paranoid/SELinux 由测试记录。
 */
@RunWith(AndroidJUnit4::class)
class PerfMechanismProbeTest {

    @Test
    fun perfMechanismProbeOnSameAppChild() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        val lines = mutableListOf<String>()

        fun record(line: String) {
            lines += line
            android.util.Log.i(TAG, line)
        }

        val assetStart = context.assets.open(DEBUG_ASSET).bufferedReader().use { it.readText() }
        val assetShaStart = sha256(assetStart)
        record("PERF-G1|asset_start|sha256=$assetShaStart|enabled0=${assetStart.contains("enabled = 0")}")
        record(
            "PERF-G1|device|model=${android.os.Build.MODEL}" +
                "|android=${android.os.Build.VERSION.RELEASE}" +
                "|sdk=${android.os.Build.VERSION.SDK_INT}" +
                "|kernel=${System.getProperty("os.version")}" +
                "|build_type=${android.os.Build.TYPE}|tags=${android.os.Build.TAGS}",
        )
        record(
            "PERF-G1|env|paranoid=${readProc("/proc/sys/kernel/perf_event_paranoid")}" +
                "|yama_ptrace_scope=${readProc("/proc/sys/kernel/yama/ptrace_scope")}" +
                "|selinux_enforce=${readProc("/sys/fs/selinux/enforce")}" +
                "|selinux_self=${readProc("/proc/self/attr/current")}" +
                "|uid=${android.os.Process.myUid()}|my_pid=${android.os.Process.myPid()}",
        )

        val parsed = A301SoDebugConfig.parse(assetStart)
        val assetPerf = requireNotNull(parsed.perf) { "debug asset perf section missing" }
        /* 仅内存启用（debug 测试注入）；asset 未改。 */
        val perf = assetPerf.copy(enabled = 1)
        val execution = A301SoDebugConfig.executionParameters()
        record(
            "PERF-G1|attr|sample_type=${perf.attrSampleType}" +
                "|period=${perf.attrSamplePeriod}" +
                "|ring_pages=${perf.ringPages}" +
                "|sample_regs_intr=${perf.attrSampleRegsIntr}" +
                "|flags=${perf.attrFlags}" +
                "|disabled=${perf.attrDisabled}|auto_enable=${perf.autoEnable}" +
                "|asset_enabled=${assetPerf.enabled}|effective_enabled=${perf.enabled}",
        )
        assertEquals("B1: PERF_SAMPLE_REGS_INTR must be 1<<18 => 262145", 262145L, perf.attrSampleType)
        assertEquals(64L, perf.ringPages)

        val native = Cve43499JniPerfNative
        val (child, childPid) = spawnChildWithPid()
        val myPid = android.os.Process.myPid().toLong()
        record("PERF-G1|child|pid=$childPid|alive=${child.isAlive}")
        val baseRequest = Native43499PerfOpenRequest(
            pid = childPid.toLong(),
            cpu = -1L,
            attrType = perf.attrType,
            attrConfig = perf.attrConfig,
            attrSamplePeriod = perf.attrSamplePeriod,
            attrSampleType = perf.attrSampleType,
            attrSampleRegsIntr = perf.attrSampleRegsIntr,
            attrFlags = perf.attrFlags,
            attrDisabled = perf.attrDisabled,
            excludeUser = perf.excludeUser,
            excludeHv = perf.excludeHv,
            excludeIdle = perf.excludeIdle,
            pageSize = execution.pageSize,
            ringPages = perf.ringPages,
            autoEnable = perf.autoEnable,
        )
        val parseConfig = Cve43499PerfParseConfig(
            identityStart = execution.identityStart,
            identityEnd = execution.identityEnd,
            tagNibble = execution.leakTagNibble,
            tagShift = execution.leakTagShift,
            registerCount = perf.registerCount,
            spRegisterIndex = perf.spRegisterIndex,
            candidateCapacity = perf.candidateCapacity,
            sampleType = perf.attrSampleType,
            taskAlignment = perf.taskAlignment,
        )
        val ringBytes = (execution.pageSize * perf.ringPages).toInt()

        /*
         * open 诊断矩阵：pid=0（self 线程）/pid=myPid（self 任务）/pid=child（跨任务）。
         * 请求无 exclude_kernel 字段（Native attr 零初始化=0，含 kernel 采样）；
         * 因此矩阵只区分「self vs 跨任务」定位 EACCES 来源。
         */
        val diag = mutableListOf<String>()
        fun openOnly(label: String, pid: Long) {
            val request = baseRequest.copy(pid = pid)
            val openResult = Native43499PerfOpenResult()
            val handle = native.open(request, openResult)
            val fact = "label=$label|pid=$pid|handle=$handle" +
                "|call=${openResult.callStatus}|errno=${openResult.systemError}"
            diag += fact
            record("PERF-G1|diag|$fact")
            if (handle > 0L) {
                val closeResult = Native43499CloseResult()
                val closeStatus = Cve43499PerfOwner.wrap(native, handle).close(closeResult)
                record("PERF-G1|diag|label=$label|closed=$closeStatus|retained=${closeResult.retainedForProcessExit}")
            }
        }
        openOnly("self_pid0", 0L)
        openOnly("self_task", myPid)
        openOnly("child_task", childPid.toLong())

        /* 采样统计（child 优先；child 全失败则用 self 的可用变体取 ABI 证据）。 */
        var openOk = false
        var openErrno = 0
        var records = 0
        var abi0 = 0
        var abi1 = 0
        var abi2 = 0
        var candidateRuns = 0
        var sampledPid = 0L
        var sampledLabel = "none"
        var childOpenErrno = 0

        fun samplePass(label: String, request: Native43499PerfOpenRequest, pass: Int) {
            val openResult = Native43499PerfOpenResult()
            val openStarted = System.nanoTime()
            val handle = native.open(request, openResult)
            val openMicros = (System.nanoTime() - openStarted) / 1000L
            record(
                "PERF-G1|pass=$pass|label=$label|open|handle=$handle" +
                    "|call=${openResult.callStatus}|errno=${openResult.systemError}" +
                    "|micros=$openMicros",
            )
            if (handle <= 0L) {
                if (request.pid == childPid.toLong()) childOpenErrno = openResult.systemError
                return
            }
            openOk = true
            sampledPid = request.pid
            sampledLabel = label
            val owner = Cve43499PerfOwner.wrap(native, handle)
            try {
                Thread.sleep(SAMPLE_WINDOW_MS)
                val ring = ByteArray(ringBytes)
                val collected = Native43499PerfCollectResult()
                val status = owner.collect(ring, collected)
                val unconsumed = collected.dataHead - collected.dataTailBefore
                record(
                    "PERF-G1|pass=$pass|label=$label|collect|status=$status" +
                        "|call=${collected.callStatus}|errno=${collected.systemError}" +
                        "|data_size=${collected.dataSize}|bytes=${collected.bytesCopied}" +
                        "|head=${collected.dataHead}|tail_before=${collected.dataTailBefore}" +
                        "|unconsumed=$unconsumed|tail_moved=${collected.tailMoved}",
                )
                if (status == 0 && collected.bytesCopied > 0L) {
                    val walk = walkRing(ring, collected, perf.registerCount)
                    records += walk.records
                    abi0 += walk.abiNone
                    abi1 += walk.abi64
                    abi2 += walk.abi32
                    record(
                        "PERF-G1|pass=$pass|label=$label|walk|records=${walk.records}" +
                            "|abi0_24=${walk.abiNone}|abi1_${EXPECTED_ABI1_BYTES}=${walk.abi64}" +
                            "|abi2=${walk.abi32}|other=${walk.other}" +
                            "|abi1_register_counts=${walk.registerCounts}",
                    )
                    when (
                        val parsedPass = Cve43499PerfSampleParser.parse(
                            ring = ring,
                            dataSize = collected.dataSize,
                            dataHead = collected.dataHead,
                            dataTailBefore = collected.dataTailBefore,
                            config = parseConfig,
                        )
                    ) {
                        is Cve43499PerfPass.Address -> {
                            candidateRuns++
                            record(
                                "PERF-G1|pass=$pass|label=$label|parse|address=0x" +
                                    java.lang.Long.toHexString(parsedPass.address) +
                                    "|votes=${parsedPass.votes}" +
                                    "|candidates=${parsedPass.candidates}" +
                                    "|sp_votes=${parsedPass.spVotes}" +
                                    "|alignment_ok=${parsedPass.alignmentOk}" +
                                    "|sp_evidence_ok=${parsedPass.spEvidenceOk}",
                            )
                        }
                        is Cve43499PerfPass.Missing ->
                            record("PERF-G1|pass=$pass|label=$label|parse|missing=${parsedPass.reason}")
                    }
                }
            } finally {
                val closeResult = Native43499CloseResult()
                val closeStatus = owner.close(closeResult)
                record(
                    "PERF-G1|pass=$pass|label=$label|close|status=$closeStatus" +
                        "|retained=${closeResult.retainedForProcessExit}",
                )
            }
        }

        try {
            fun opened(label: String): Boolean = diag.any {
                it.startsWith("label=$label|") && !it.contains("|handle=0|")
            }
            if (opened("child_task")) {
                repeat(PASSES) { pass -> samplePass("child_task", baseRequest, pass) }
            } else if (opened("self_task")) {
                repeat(SELF_PASSES) { pass ->
                    samplePass("self_task", baseRequest.copy(pid = myPid), pass)
                }
            } else if (opened("self_pid0")) {
                repeat(SELF_PASSES) { pass ->
                    samplePass("self_pid0", baseRequest.copy(pid = 0L), pass)
                }
            }
        } finally {
            child.destroy()
            child.waitFor(5, TimeUnit.SECONDS)
            child.destroyForcibly()
            record("PERF-G1|child_exit=${runCatching { child.exitValue() }.getOrDefault(-999)}")
        }
        openErrno = childOpenErrno

        val childDiagErrno = diag.firstOrNull { it.startsWith("label=child_task|") }
            ?.substringAfter("errno=")?.toIntOrNull() ?: 0
        val appOpenDenied = diag.all { !it.contains("handle=") || it.contains("|handle=0|") }
        record(
            "PERF-G1|summary|open_ok=$openOk|open_errno=$openErrno" +
                "|child_diag_errno=$childDiagErrno|app_open_denied=$appOpenDenied" +
                "|sampled_label=$sampledLabel|sampled_pid=$sampledPid|records=$records" +
                "|abi0_24=$abi0|abi1_${EXPECTED_ABI1_BYTES}=$abi1|abi2=$abi2" +
                "|candidate_passes=$candidateRuns",
        )
        record("PERF-G1|diag_matrix|${diag.joinToString(";")}")
        /* G1 结论行：App 域全部 EACCES ⇒ App 跨任务 perf 不可用。 */
        record(
            "PERF-G1|conclusion|app_perf_open=" +
                (if (appOpenDenied) "denied_eacces" else "allowed") +
                "|errno=$childDiagErrno|kernel_regs_delivery=待 shell/root 证（见 device-gates）",
        )

        val assetEnd = context.assets.open(DEBUG_ASSET).bufferedReader().use { it.readText() }
        val assetShaEnd = sha256(assetEnd)
        record(
            "PERF-G1|asset_end|sha256=$assetShaEnd|unchanged=${assetShaStart == assetShaEnd}" +
                "|enabled0=${assetEnd.contains("enabled = 0")}",
        )
        val report = File(context.filesDir, REPORT_FILE)
        report.writeText(lines.joinToString("\n"))
        record("PERF-G1|report_file=${report.absolutePath}|lines=${lines.size}")

        /* 恢复证据：asset 从未被修改，且 perf.enabled 仍为 0。 */
        assertEquals("debug asset must stay unchanged", assetShaStart, assetShaEnd)
        assertTrue("debug asset must keep perf.enabled=0", assetEnd.contains("enabled = 0"))

        if (!openOk) {
            fail(
                "App-domain perf_event_open denied (G1 app-level FAIL): " +
                    "matrix=${diag.joinToString(";")} (see PERF-G1 logcat/report)",
            )
        }
        val regsRecords = abi1 + abi2
        assertTrue(
            "expected >=1 regs record (ABI=1/2, ${EXPECTED_ABI1_BYTES}B), got abi0=$abi0 abi1=$abi1 abi2=$abi2",
            regsRecords > 0,
        )
    }

    /**
     * 启动同 app UID 的 shell 子进程并取得 pid（shell 先 echo $$；失败用反射/重试）。
     *
     * @return 运行中的子进程 + pid。
     * @throws IllegalStateException 3 次都取不到 pid（含子进程过早退出原因）。
     */
    private fun spawnChildWithPid(): Pair<Process, Int> {
        var lastExit = "none"
        repeat(3) {
            val child = ProcessBuilder(
                "/system/bin/sh",
                "-c",
                "echo \$\$; while true; do /system/bin/true; done",
            ).redirectErrorStream(true).start()
            val reader = child.inputStream.bufferedReader()
            val deadline = System.nanoTime() + 2_000_000_000L
            var pid: Int? = null
            while (pid == null && System.nanoTime() < deadline) {
                val reflected = pidOf(child)
                if (reflected > 0) {
                    pid = reflected
                    break
                }
                if (reader.ready()) {
                    pid = reader.readLine()?.trim()?.toIntOrNull()
                    break
                }
                Thread.sleep(20L)
            }
            if (pid != null) return child to pid
            lastExit = runCatching { child.exitValue().toString() }.getOrDefault("alive")
            child.destroyForcibly()
            child.waitFor(1, TimeUnit.SECONDS)
        }
        error("child shell did not report pid (last_exit=$lastExit)")
    }

    /**
     * 反射读取 Android libcore ProcessImpl 的 pid 字段（java.lang.Process 无 pid()）。
     *
     * @param process 已启动子进程。
     * @return pid；读不到返回 -1。
     */
    private fun pidOf(process: Process): Int {
        var type: Class<*>? = process.javaClass
        while (type != null) {
            try {
                val field = type.getDeclaredField("pid")
                field.isAccessible = true
                return (field.get(process) as Number).toInt()
            } catch (_: ReflectiveOperationException) {
                /* 继续查父类字段。 */
            }
            type = type.superclass
        }
        return -1
    }

    /**
     * 诊断性 ring 逐 record 走查（只读；与 Cve43499PerfSampleParser 同一 ABI 语义）。
     *
     * @param ring Native 整块拷贝的环副本。
     * @param collected collect 回执（绝对 head/tail）。
     * @param registerCount 期望寄存器数。
     * @return 记录/ABI 分布统计。
     */
    private fun walkRing(
        ring: ByteArray,
        collected: Native43499PerfCollectResult,
        registerCount: Int,
    ): WalkStats {
        val dataSize = collected.dataSize
        if (dataSize <= 0L || dataSize > ring.size.toLong()) return WalkStats(0, 0, 0, 0, 0, "")
        val head = collected.dataHead
        var position = collected.dataTailBefore
        val maxRecords = ((head - position).coerceAtLeast(0L) / 8L).toInt() + 8
        var seen = 0
        var records = 0
        var abiNone = 0
        var abi64 = 0
        var abi32 = 0
        var other = 0
        val registerCounts = LinkedHashSet<Int>()
        while (position < head && seen < maxRecords) {
            seen++
            val remaining = head - position
            if (remaining < 8L) break
            val type = readU32(ring, dataSize, position)
            val size = readU16(ring, dataSize, position + 6L)
            if (size == 0L || size > remaining) break
            records++
            if (type == 9L && size >= 24L) {
                val abi = readU64(ring, dataSize, position + 16L)
                when (abi) {
                    0L -> if (size == 24L) abiNone++ else other++
                    1L -> {
                        abi64++
                        registerCounts += ((size - 24L) / 8L).toInt()
                    }
                    2L -> abi32++
                    else -> other++
                }
            } else {
                other++
            }
            position += size
        }
        val counts = registerCounts.joinToString(",") {
            if (it == registerCount) "$it(expected)" else it.toString()
        }
        return WalkStats(records, abiNone, abi64, abi32, other, counts)
    }

    private fun readU16(bytes: ByteArray, dataSize: Long, position: Long): Long {
        var value = 0L
        for (index in 0 until 2) {
            value = value or (readByte(bytes, dataSize, position + index).toLong() shl (8 * index))
        }
        return value
    }

    private fun readU32(bytes: ByteArray, dataSize: Long, position: Long): Long {
        var value = 0L
        for (index in 0 until 4) {
            value = value or (readByte(bytes, dataSize, position + index).toLong() shl (8 * index))
        }
        return value
    }

    private fun readU64(bytes: ByteArray, dataSize: Long, position: Long): Long {
        var value = 0L
        for (index in 0 until 8) {
            value = value or (readByte(bytes, dataSize, position + index).toLong() shl (8 * index))
        }
        return value
    }

    private fun readByte(bytes: ByteArray, dataSize: Long, position: Long): Int =
        bytes[(position % dataSize).toInt()].toInt() and 0xFF

    private fun readProc(path: String): String =
        runCatching { File(path).readText().trim() }.getOrDefault("unreadable")

    private fun sha256(text: String): String =
        MessageDigest.getInstance("SHA-256")
            .digest(text.toByteArray())
            .joinToString("") { "%02x".format(it) }

    /** ring 走查统计。 */
    private data class WalkStats(
        val records: Int,
        val abiNone: Int,
        val abi64: Int,
        val abi32: Int,
        val other: Int,
        val registerCounts: String,
    )

    private companion object {
        const val TAG = "PERF-G1"
        const val PASSES = 3
        const val SELF_PASSES = 1
        const val SAMPLE_WINDOW_MS = 400L
        const val EXPECTED_ABI1_BYTES = 280
        const val DEBUG_ASSET = "profile/43499/debug-a301so.conf"
        const val REPORT_FILE = "perf-g1-report.txt"
    }
}
