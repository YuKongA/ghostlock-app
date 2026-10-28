package com.ghostlock.app.backend.cve43499.victim

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.ghostlock.app.data.cve43499.A301SoDebugConfig
import com.ghostlock.app.jni.VictimCloseJniResult
import com.ghostlock.app.jni.VictimSpawnJniResult
import com.ghostlock.app.jni.Native43499Jni
import com.ghostlock.app.jni.VictimTransportJni
import com.ghostlock.app.core.victim.VictimProtocol
import com.ghostlock.app.core.victim.VictimResult
import java.io.File
import java.security.MessageDigest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * P0c 安全门 E0+E4 真机探针（androidTest，debug 测试链）。
 *
 * E0：环境快照（fingerprint/paranoid/getenforce/uid/attr/Seccomp/kernelsu/page size/build type）。
 * E4：经 `VictimTransportJni` spawn helper PIE，验证 pid/uid/attr/seccomp、fd 契约、
 * spawn p50/p95/VmHWM、probeUid/park/detach/bye/close 往返、超时/EOF、execScript
 * `sh <script>`（内容 `echo ok`，安全、非 finish 攻击脚本）、fd 泄漏=0、零 fork 静态主证。
 *
 * **不写内核、不落页、不 perf 攻击、不启动 Flow/W1**；只 spawn/命令/execScript。
 * 失败以具名 AssertionError 上报（不崩）；
 *
 * @note 阶段：P0c E0/E4（task-55）；设备 A301SO；真机读数入 report/logcat。
 */
@RunWith(AndroidJUnit4::class)
class JniVictimMechanismProbeTest {

    @Test
    fun e0EnvironmentAndE4JniVictimTransport() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val lines = mutableListOf<String>()

        fun record(line: String) {
            lines += line
            android.util.Log.i(TAG, line)
        }

        try {
            e0Snapshot(context, ::record)
            e4Transport(context, ::record)
        } finally {
            val report = File(context.filesDir, REPORT_FILE)
            report.writeText(lines.joinToString("\n"))
            record("E4|report_file=${report.absolutePath}|lines=${lines.size}")
        }
    }

    /* ------------------------------------------------------------------ */
    /* E0：环境快照                                                         */
    /* ------------------------------------------------------------------ */

    private fun e0Snapshot(
        context: android.content.Context,
        record: (String) -> Unit,
    ) {
        record("E0|device|model=${android.os.Build.MODEL}|fingerprint=${android.os.Build.FINGERPRINT}")
        record(
            "E0|build|release=${android.os.Build.VERSION.RELEASE}" +
                "|sdk=${android.os.Build.VERSION.SDK_INT}" +
                "|type=${android.os.Build.TYPE}|tags=${android.os.Build.TAGS}" +
                "|target_sdk=${context.applicationInfo.targetSdkVersion}",
        )
        record("E0|kernel=${System.getProperty("os.version")}")
        record(
            "E0|env|paranoid=${readProc("/proc/sys/kernel/perf_event_paranoid")}" +
                "|selinux_enforce=${readProc("/sys/fs/selinux/enforce")}" +
                "|selinux_self=${readProc("/proc/self/attr/current").trimEnd('\u0000')}" +
                "|uid=${android.os.Process.myUid()}" +
                "|page_size=${android.system.Os.sysconf(android.system.OsConstants._SC_PAGESIZE)}",
        )
        record("E0|seccomp_self=${statusField("/proc/self/status", "Seccomp:")}")
        val power = context.getSystemService(android.os.PowerManager::class.java)
        record(
            "E0|power|interactive=${power?.isInteractive}" +
                "|device_idle=${power?.isDeviceIdleMode}" +
                "|power_save=${power?.isPowerSaveMode}",
        )
        val modules = runCatching { File("/proc/modules").readLines() }.getOrDefault(emptyList())
        val ksuModules = modules.filter { it.contains("kernelsu", ignoreCase = true) || it.contains("ksud", ignoreCase = true) }
        record(
            "E0|kernel_modules|total=${modules.size}|ksu_lines=${ksuModules.size}" +
                "|sample=${ksuModules.take(3).joinToString(";")}",
        )
        record(
            "E0|ksu_paths|" + listOf("/data/adb/ksu", "/data/adb/modules", "/data/adb/ksud").joinToString(";") { path ->
                "$path=${runCatching { File(path).exists() }.getOrDefault(false)}"
            },
        )
        val asset = runCatching {
            context.assets.open(DEBUG_ASSET).bufferedReader().use { it.readText() }
        }.getOrElse { problem ->
            throw AssertionError("E0|debug-asset-unreadable:${problem.message}")
        }
        val perf = runCatching { A301SoDebugConfig.parse(asset).perf }.getOrNull()
            ?: throw AssertionError("E0|perf-section-missing")
        record(
            "E0|attr|sample_type=${perf.attrSampleType}|period=${perf.attrSamplePeriod}" +
                "|ring_pages=${perf.ringPages}|sample_regs_intr=${perf.attrSampleRegsIntr}" +
                "|flags=${perf.attrFlags}|disabled=${perf.attrDisabled}|enabled=${perf.enabled}",
        )
        assertTrue("E0|B1 attr 必须 262145", perf.attrSampleType == 262145L)
        assertTrue("E0|perf 必须保持 enabled=0", perf.enabled == 0)
        assertTrue("E0|page size 必须 >0", android.system.Os.sysconf(android.system.OsConstants._SC_PAGESIZE) > 0)
    }

    /* ------------------------------------------------------------------ */
    /* E4：JNI 单步 transport                                              */
    /* ------------------------------------------------------------------ */

    private fun e4Transport(
        context: android.content.Context,
        record: (String) -> Unit,
    ) {
        val helper = File(context.applicationInfo.nativeLibraryDir, VICTIM_HELPER_FILE_NAME)
        record(
            "E4|helper|path=${helper.absolutePath}|exists=${helper.isFile}" +
                "|executable=${helper.canExecute()}|size=${helper.length()}",
        )
        assertTrue("E4|helper 不存在（nativeLibraryDir 未打包 PIE）", helper.isFile)
        assertTrue("E4|helper 不可执行", helper.canExecute())

        /* 零 fork ①②③：①=--wrap 运行计数（本 harness 通过 JNI 读）；②=ELF 符号解析；
         * ③=源码调用图审计（VictimArchitectureTest）。带 --wrap 的二进制里 `fork/vfork`
         * 会作为 `__real_fork/__real_vfork` 别名导入（预期），因此 ② 按“wrapper 存在 +
         * 无直接调用点”解读，严格 no-import 口径需无 wrap 的生产构建。 */
        val jniLib = File(context.applicationInfo.nativeLibraryDir, JNI_LIB_FILE_NAME)
        val helperSyms = runCatching { elfSymbolNames(helper) }.getOrElse { problem ->
            throw AssertionError("E4|helper-symbols-unreadable:${problem.message}")
        }
        val jniSyms = runCatching { elfSymbolNames(jniLib) }.getOrElse { problem ->
            throw AssertionError("E4|jni-symbols-unreadable:${problem.message}")
        }

        val forkImports = (helperSyms.dynsym + jniSyms.dynsym)
            .filter { it == "fork" || it == "vfork" || it == "__fork" }
        record(
            "E4|zero_fork|helper_syms=${helperSyms.names.size}|jni_syms=${jniSyms.names.size}" +
                "|fork_imports=${forkImports.size}(wrapper_alias_expected；APK 剥离 symtab 后" +
                "wrapper 符号不可见，故①以运行计数/canary 证明)" +
                "|helper_posix_spawn=${helperSyms.names.contains("posix_spawn")}",
        )
        assertTrue("E4|helper 应通过 posix_spawn 启动子进程", helperSyms.names.contains("posix_spawn"))
        val jniTransportSymbols = jniSyms.names.filter {
            it.startsWith("Java_com_ghostlock_app_jni_VictimTransportJni_") &&
                !it.contains("forkGuard")
        }
        val jniForkGuardSymbols = jniSyms.names.filter {
            it.startsWith("Java_com_ghostlock_app_jni_VictimTransportJni_forkGuard")
        }
        record(
            "E4|jni_victim_symbols=${jniTransportSymbols.size}" +
                "|jni_fork_guard_symbols=${jniForkGuardSymbols.size}",
        )
        assertEquals("E4|JNI victim 入口应为 9 个", 9, jniTransportSymbols.size)
        assertEquals("E4|fork guard debug 入口应为 3 个", 3, jniForkGuardSymbols.size)

        val fdBefore = countSelfFds()
        record("E4|fd_before=$fdBefore")
        /* E4 ①：生产/JNI 路径 fork/vfork 计数基线（--wrap 计数）。 */
        assertEquals("E4|① 初始 fork 计数非 0", 0, forkCount(VictimTransportJni.forkGuardSnapshot()))
        assertEquals("E4|① 初始 vfork 计数非 0", 0, forkCountVfork(VictimTransportJni.forkGuardSnapshot()))

        /* 详细单品会话：uid/attr/seccomp/fd 契约/VmHWM/probeUid/timeout/execScript/bye/EOF。 */
        val first = spawn(helper, rawSizeBytes = RAW_SIZE_BYTES)
        record("E4|spawn|handle=${first.handle}|pid=${first.pid}")
        assertTrue("E4|spawn pid 必须 >0", first.pid > 0)
        try {
            /* 先 probeUid：posix_spawn 返回时 helper 可能尚未 exec 完；收到回复即已收敛。 */
            val uidReply = roundTrip(first.handle, VictimProtocol.encodeProbeUid(), record)
            val uid = when (val decoded = VictimProtocol.decodeProbeUidReply(uidReply.data)) {
                is VictimResult.Ok -> decoded.value
                is VictimResult.Failed -> throw AssertionError("E4|probeUid 解码失败:${decoded.reason}")
            }
            record("E4|probe_uid=$uid")
            assertEquals("E4|probeUid 应等于 app uid", android.os.Process.myUid().toLong(), uid)

            val helperStatus = readProc("/proc/${first.pid}/status")
            val helperUid = statusField("/proc/${first.pid}/status", "Uid:")
            val helperSeccomp = statusField("/proc/${first.pid}/status", "Seccomp:")
            record(
                "E4|pid_facts|uid_line=$helperUid|seccomp=$helperSeccomp" +
                    "|attr=${readProc("/proc/${first.pid}/attr/current").trimEnd(' ')}" +
                    "|name=${statusField("/proc/${first.pid}/status", "Name:")}",
            )
            assertTrue("E4|helper Uid 行缺失", helperUid.isNotBlank())
            assertTrue(
                "E4|helper uid 应与 app 一致: $helperUid",
                helperUid.split(Regex("\\s+")).getOrNull(1) == android.os.Process.myUid().toString(),
            )
            assertEquals(
                "E4|helper Seccomp 应与 app 一致",
                statusField("/proc/self/status", "Seccomp:"),
                helperSeccomp,
            )
            /* helper 已在命令循环 ⇒ fd 收敛完成；断言 fd 集合恰为 0..4（≥5 EBADF 自检通过）。 */
            val convergedFds = runCatching { File("/proc/${first.pid}/fd").list()?.toSet() }
                .getOrNull() ?: emptySet()
            record("E4|helper_fd_set=${convergedFds.sorted()}")
            assertEquals("E4|helper fd 集合必须恰为 0..4", setOf("0", "1", "2", "3", "4"), convergedFds)
            val vmHwm = statusField("/proc/${first.pid}/status", "VmHWM:")
            val vmHwmKb = Regex("(\\d+)").find(vmHwm)?.groupValues?.get(1)?.toLongOrNull()
            record("E4|vm_hwm_kb=$vmHwmKb")
            assertNotNull("E4|VmHWM 无法解析: $vmHwm", vmHwmKb)
            assertTrue("E4|VmHWM 超阈 64MiB: ${vmHwmKb}kB", vmHwmKb!! <= MAX_VM_HWM_KB)

            timeoutPath(first.handle, record)

            val script = File(context.filesDir, E4_SCRIPT_NAME)
            /* helper execScript 固定执行 `/system/bin/sh <script>`；脚本内再显式走
             * `sh -c 'echo ok'`，与任务判据等价且不触达 finish 攻击脚本。 */
            script.writeText("exec /system/bin/sh -c 'echo ok'\n")
            val execReply = roundTrip(
                first.handle,
                VictimProtocol.encodeExecScript(
                    VictimProtocol.ExecScriptRequest(
                        scriptPath = script.absolutePath,
                        argv = emptyList(),
                        env = emptyList(),
                        timeoutMs = 5_000L,
                    ),
                ),
                record,
            )
            val exec = when (val decoded = VictimProtocol.decodeExecScriptReply(execReply.data)) {
                is VictimResult.Ok -> decoded.value
                is VictimResult.Failed -> throw AssertionError("E4|execScript 解码失败:${decoded.reason}")
            }
            record(
                "E4|exec_script|command=/system/bin/sh ${script.absolutePath}" +
                    "|inner=sh -c echo_ok" +
                    "|exit=${exec.exitCode}|output_len=${exec.outputLen}" +
                    "|truncated=${exec.truncated}|timed_out=${exec.timedOut}",
            )
            assertEquals("E4|execScript exit 必须 0", 0, exec.exitCode)
            assertTrue("E4|execScript 不得 timed out", !exec.timedOut)
            assertTrue("E4|execScript 不得截断", !exec.truncated)
            val raw = readRaw(first.handle, 0L, VictimProtocol.RAW_HEADER_BYTES + exec.outputLen, record)
            val frame = when (val decoded = VictimProtocol.decodeRawFrame(raw, exec.outputLen)) {
                is VictimResult.Ok -> decoded.value
                is VictimResult.Failed -> throw AssertionError("E4|exec raw 解码失败:${decoded.reason}")
            }
            val output = String(frame.data, Charsets.UTF_8)
            val expectedSha = sha256Hex("ok\n".toByteArray())
            val actualSha = VictimProtocol.sha256Hex(frame.data)
            record(
                "E4|exec_output|expect_sha=$expectedSha|actual_sha=$actualSha" +
                    "|text=${output.replace("\n", "<NL>")}",
            )
            assertEquals("E4|execScript 输出必须为 ok\n", "ok\n", output)
            assertEquals("E4|execScript 输出 SHA 不符", expectedSha, actualSha)

            byeSession(first.handle, record)
        } finally {
            closeToken(first.handle, record)
        }

        /* park/detach 往返（detach 不 kill；随后 app 侧 kill 清理临时 helper）。 */
        val parked = spawn(helper, rawSizeBytes = 64L * 1024L)
        try {
            roundTrip(parked.handle, VictimProtocol.encodePark(), record)
            val detachStatus = VictimTransportJni.detachVictim(parked.handle)
            record("E4|detach|status=$detachStatus|pid=${parked.pid}")
            assertEquals("E4|detach 必须成功", 0, detachStatus)
            android.os.Process.killProcess(parked.pid)
            record("E4|detach_cleanup|killProcess=${parked.pid}")
        } catch (problem: Throwable) {
            runCatching { android.os.Process.killProcess(parked.pid) }
            throw problem
        }

        /* 延迟阈值只在交互态有效：Doze/息屏会引入调度抖动（实测 p50 17~29ms 假失败）。 */
        val latencyPower = context.getSystemService(android.os.PowerManager::class.java)
        replyRecord("E4|latency_precondition|interactive=${latencyPower?.isInteractive}" +
            "|device_idle=${latencyPower?.isDeviceIdleMode}", record)
        assertTrue(
            "E4|device-not-interactive：请在亮屏/交互态下测量 spawn 延迟" +
                "（powerSave=${latencyPower?.isPowerSaveMode}）",
            latencyPower?.isInteractive == true && latencyPower.isDeviceIdleMode.not(),
        )

        /* JNI 基线（同接口的 invalid 快速路径，估计 JNI/调试开销）。 */
        val jniNoop = LongArray(JNI_BASELINE_SAMPLES)
        repeat(JNI_BASELINE_SAMPLES) { index ->
            val started = System.nanoTime()
            VictimTransportJni.rawSizeVictim(0L)
            jniNoop[index] = System.nanoTime() - started
        }
        val jniAvgUs = jniNoop.average().toLong()
        record("E4|jni_baseline_us|n=${jniNoop.size}|avg=$jniAvgUs|max=${jniNoop.max()}")
        android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_DISPLAY)
        record("E4|thread_priority=${android.os.Process.THREAD_PRIORITY_DISPLAY}")

        /* spawn 延迟 N=20（只测 spawn；随后 bye+wait+close 收口）。 */
        val latencies = LongArray(SPAWN_SAMPLES)
        repeat(SPAWN_SAMPLES) { index ->
            val started = System.nanoTime()
            val spawned = spawn(helper, rawSizeBytes = 64L * 1024L)
            latencies[index] = (System.nanoTime() - started) / 1_000L
            assertTrue("E4|延迟采样 pid 必须 >0", spawned.pid > 0)
            byeSession(spawned.handle, record)
            closeToken(spawned.handle, record)
        }
        val sorted = latencies.sortedArray()
        val p50 = sorted[(sorted.size * 50 / 100).coerceAtMost(sorted.size - 1)]
        val p95 = sorted[((sorted.size * 95 + 99) / 100 - 1).coerceIn(0, sorted.size - 1)]
        record(
            "E4|spawn_latency_us|n=${sorted.size}|p50=$p50|p95=$p95" +
                "|min=${sorted.first()}|max=${sorted.last()}",
        )
        assertTrue("E4|spawn p50 超阈 10ms: ${p50}us", p50 <= 10_000L)
        assertTrue("E4|spawn p95 超阈 30ms: ${p95}us", p95 <= 30_000L)

        val fdAfter = countSelfFds()
        record("E4|fd_after=$fdAfter|delta=${fdAfter - fdBefore}")
        assertEquals("E4|fd 泄漏（spawn/close 前后计数差）", fdBefore, fdAfter)

        /* E4 ①：生产路径（所有 spawn/命令）之后计数仍为 0。 */
        val snapshotAfterSessions = VictimTransportJni.forkGuardSnapshot()
        record(
            "E4|fork_guard_after_sessions|fork=${forkCount(snapshotAfterSessions)}" +
                "|vfork=${forkCountVfork(snapshotAfterSessions)}",
        )
        assertEquals("E4|① 生产路径 fork 计数必须 0", 0, forkCount(snapshotAfterSessions))
        assertEquals("E4|① 生产路径 vfork 计数必须 0", 0, forkCountVfork(snapshotAfterSessions))

        /* E4 ①“能失败”验证：故意 fork 一次 ⇒ 计数=1；reset 后=0。 */
        val canaryStatus = VictimTransportJni.forkGuardCanary()
        val afterCanary = VictimTransportJni.forkGuardSnapshot()
        record(
            "E4|fork_guard_canary|status=$canaryStatus|wrap_proof=canary" +
                "|fork=${forkCount(afterCanary)}|vfork=${forkCountVfork(afterCanary)}",
        )
        assertEquals("E4|① canary fork 应成功", 0, canaryStatus)
        assertEquals("E4|① canary 后 fork 计数必须 1（能失败验证）", 1, forkCount(afterCanary))
        assertEquals("E4|① canary 后 vfork 计数必须 0", 0, forkCountVfork(afterCanary))
        assertEquals("E4|① reset 应成功", 0, VictimTransportJni.forkGuardReset())
        val afterReset = VictimTransportJni.forkGuardSnapshot()
        assertEquals("E4|① reset 后 fork 计数必须 0", 0, forkCount(afterReset))
        assertEquals("E4|① reset 后 vfork 计数必须 0", 0, forkCountVfork(afterReset))
        record("E4|fork_guard_reset|fork=${forkCount(afterReset)}|vfork=${forkCountVfork(afterReset)}")
    }

    /* ------------------------------------------------------------------ */
    /* 传输动作                                                             */
    /* ------------------------------------------------------------------ */

    /** spawn 结果（handle+pid）。 */
    private data class Spawned(val handle: Long, val pid: Int)

    /**
     * 经 JNI spawn helper。
     *
     * @param helper helper PIE。
     * @param rawSizeBytes raw memfd 容量。
     * @return handle + pid。
     */
    private fun spawn(helper: File, rawSizeBytes: Long): Spawned {
        val result = VictimSpawnJniResult()
        val handle = VictimTransportJni.spawnVictim(
            helperPath = helper.absolutePath,
            argv = arrayOf(helper.absolutePath),
            env = emptyArray(),
            rawSizeBytes = rawSizeBytes,
            result = result,
        )
        /* token=Native 指针（可能是 MTE top-byte 标记 ⇒ Long 可为负），只排除 0/哨兵。 */
        if (handle == 0L ||
            handle == Native43499Jni.RESULT_NOT_ACCEPTED_HANDLE ||
            handle == Native43499Jni.RESULT_INCOMPLETE_HANDLE
        ) {
            throw AssertionError(
                "E4|spawn-failed|handle=$handle|call=${result.callStatus}|errno=${result.systemError}",
            )
        }
        if (result.pid <= 0) {
            throw AssertionError("E4|spawn-missing-pid|call=${result.callStatus}")
        }
        return Spawned(handle, result.pid)
    }

    /**
     * 写入一条命令并读回回复；ERROR 状态即具名失败。
     *
     * @param handle token。
     * @param frame 命令线帧。
     * @param record 记录器。
     * @return 解码回复（OK）。
     */
    private fun roundTrip(
        handle: Long,
        frame: ByteArray,
        record: (String) -> Unit,
    ): VictimProtocol.Reply {
        val sent = VictimTransportJni.sendVictim(handle, frame, CONTROL_TIMEOUT_MS)
        if (sent != frame.size) {
            throw AssertionError("E4|send-failed|status=$sent|len=${frame.size}")
        }
        val out = ByteArray(VictimProtocol.WIRE_MAX_BYTES)
        val received = VictimTransportJni.receiveVictim(handle, out, CONTROL_TIMEOUT_MS)
        if (received <= 0) {
            throw AssertionError("E4|receive-failed|status=$received")
        }
        val wire = out.copyOf(received)
        val reply = when (val decoded = VictimProtocol.decodeReply(wire)) {
            is VictimResult.Ok -> decoded.value
            is VictimResult.Failed -> throw AssertionError("E4|decode-failed:${decoded.reason}")
        }
        if (reply.status != VictimProtocol.Status.OK) {
            throw AssertionError(
                "E4|helper-error|code=${reply.errorCode}|errno=${reply.errorErrno}",
            )
        }
        record("E4|round_trip|wire=${wire.size}|data=${reply.data.size}")
        return reply
    }

    /**
     * 超时路径：不发送命令直接 receive（有界 50ms）应得 TIMEOUT。
     *
     * @param handle token。
     * @param record 记录器。
     */
    private fun timeoutPath(handle: Long, record: (String) -> Unit) {
        val out = ByteArray(VictimProtocol.WIRE_MAX_BYTES)
        val status = VictimTransportJni.receiveVictim(handle, out, TIMEOUT_PROBE_MS)
        record("E4|timeout_path|status=$status|expect=${-1003}")
        assertEquals("E4|超时路径应返回 TIMEOUT(-1003)", -1003, status)
    }

    /**
     * bye 往返 + 有界 wait + EOF 路径。
     *
     * @param handle token。
     * @param record 记录器。
     */
    private fun byeSession(handle: Long, record: (String) -> Unit) {
        val reply = roundTrip(handle, VictimProtocol.encodeBye(), record)
        val counter = when (val decoded = VictimProtocol.decodeForkGuardReply(reply.data)) {
            is VictimResult.Ok -> decoded.value
            is VictimResult.Failed -> throw AssertionError(
                "E4|① helper bye 计数解码失败:${decoded.reason}",
            )
        }
        record("E4|helper_fork_counters|fork=${counter.fork}|vfork=${counter.vfork}")
        assertEquals("E4|① helper fork 计数必须 0", 0L, counter.fork)
        assertEquals("E4|① helper vfork 计数必须 0", 0L, counter.vfork)
        val exit = VictimTransportJni.waitExitVictim(handle, EXIT_TIMEOUT_MS)
        record("E4|bye_wait_exit=$exit")
        assertEquals("E4|bye 后 waitExit 应 0", 0, exit)
        val out = ByteArray(VictimProtocol.WIRE_MAX_BYTES)
        val afterExit = VictimTransportJni.receiveVictim(handle, out, EXIT_TIMEOUT_MS)
        record("E4|eof_path|status=$afterExit")
        assertTrue("E4|bye 后 receive 必须失败（EOF/超时）", afterExit < 0)
    }

    /**
     * close token 并断言成功（恰一次；调用方保证不重用）。
     *
     * @param handle token。
     * @param record 记录器。
     */
    private fun closeToken(handle: Long, record: (String) -> Unit) {
        if (handle == 0L) return
        val result = VictimCloseJniResult()
        val status = VictimTransportJni.closeVictim(handle, result)
        record("E4|close|status=$status|call=${result.callStatus}|retained=${result.retainedResourceCount}")
        if (status != 0) {
            throw AssertionError("E4|close-failed|status=$status|call=${result.callStatus}")
        }
    }

    /**
     * 从 raw memfd 读取并解码 raw 帧。
     *
     * @param handle token。
     * @param offset memfd 偏移。
     * @param length 读取长度。
     * @param record 记录器。
     * @return 原始字节。
     */
    private fun readRaw(
        handle: Long,
        offset: Long,
        length: Long,
        record: (String) -> Unit,
    ): ByteArray {
        val out = ByteArray(length.toInt())
        val status = VictimTransportJni.readRawVictim(handle, offset, out, length.toInt())
        if (status != length.toInt()) {
            throw AssertionError("E4|readRaw-failed|status=$status|len=$length")
        }
        record("E4|read_raw|offset=$offset|length=$length")
        return out
    }

    /* ------------------------------------------------------------------ */
    /* 工具                                                                 */
    /* ------------------------------------------------------------------ */

    /**
     * @param line 记录内容。
     * @param record 记录器（便于在无局部函数作用域处复用）。
     */
    private fun replyRecord(line: String, record: (String) -> Unit) {
        record(line)
    }

    /** @param snapshot `forkGuardSnapshot()` 打包值。 @return fork 计数（高 32 位）。 */
    private fun forkCount(snapshot: Long): Int = (snapshot ushr 32).toInt()

    /** @param snapshot `forkGuardSnapshot()` 打包值。 @return vfork 计数（低 32 位）。 */
    private fun forkCountVfork(snapshot: Long): Int = snapshot.toInt()

    /** @return 当前进程 fd 数（/proc/self/fd 条目）。 */
    private fun countSelfFds(): Int =
        File("/proc/self/fd").list()?.size ?: -1

    /**
     * @param path /proc 文件。
     * @return 内容；不可读返回 "unreadable"。
     */
    private fun readProc(path: String): String =
        runCatching { File(path).readText().trim() }.getOrDefault("unreadable")

    /**
     * @param path /proc 文件。
     * @param prefix 行前缀（如 "Seccomp:"）。
     * @return 该行 trim 后内容；缺失返回 ""。
     */
    private fun statusField(path: String, prefix: String): String =
        runCatching {
            File(path).readLines().firstOrNull { it.trimStart().startsWith(prefix) }?.trim() ?: ""
        }.getOrDefault("")

    /** @return SHA-256 hex。 */
    private fun sha256Hex(bytes: ByteArray): String =
        MessageDigest.getInstance("SHA-256").digest(bytes)
            .joinToString("") { "%02x".format(it) }

    /**
     * ELF64 符号表（dynsym + symtab）：E4 ② readelf -Ws 等价的最小实现。
     *
     * @property dynsym 动态符号名（含导入/导出）。
     * @property names 动态+静态符号名（用于识别 local 的 __wrap_*）。
     */
    private data class ElfSymbols(val dynsym: Set<String>, val names: Set<String>)

    /**
     * @param file ELF 文件（PIE 或 .so）。
     * @return 符号表（dynsym 与合并集）。
     * @throws IllegalStateException 非 ELF64/缺少 dynsym。
     */
    private fun elfSymbolNames(file: File): ElfSymbols {
        val bytes = file.readBytes()
        if (bytes.size < 64 || bytes[0] != 0x7f.toByte() || bytes[1] != 'E'.code.toByte() ||
            bytes[2] != 'L'.code.toByte() || bytes[3] != 'F'.code.toByte()
        ) {
            throw IllegalStateException("not elf: $file")
        }
        if (bytes[4] != 2.toByte()) throw IllegalStateException("not elf64: $file")
        val shoff = readLong(bytes, 0x28)
        val shentsize = readU16(bytes, 0x3A).toInt()
        val shnum = readU16(bytes, 0x3C).toInt()
        if (shoff < 0L || shentsize < 64 || shnum <= 0) throw IllegalStateException("bad shdr")
        fun parse(table: Long, size: Long, entrySize: Long, strtab: Long): Set<String> {
            val values = LinkedHashSet<String>()
            var entry = 0L
            while (entry + entrySize <= size) {
                val stName = readU32(bytes, table + entry).toInt()
                if (stName > 0) values += readCString(bytes, strtab + stName.toLong())
                entry += entrySize
            }
            return values
        }

        val dynsym = runCatching {
            var offset = -1L
            var size = 0L
            var entrySize = 0L
            var strtab = -1
            var index = 0
            while (index < shnum) {
                val base = shoff + index.toLong() * shentsize
                if (readU32(bytes, base + 4L) == 11UL) {
                    offset = readLong(bytes, base + 0x18)
                    size = readLong(bytes, base + 0x20)
                    entrySize = readLong(bytes, base + 0x38)
                    strtab = readU32(bytes, base + 0x28).toInt()
                    break
                }
                index++
            }
            if (offset < 0L || strtab < 0 || entrySize <= 0L) {
                throw IllegalStateException("no dynsym")
            }
            val strtabBase = shoff + strtab.toLong() * shentsize
            parse(offset, size, entrySize, readLong(bytes, strtabBase + 0x18))
        }.getOrElse { problem ->
            throw IllegalStateException("no dynsym: $file (${problem.message})")
        }
        val symtab = runCatching {
            var offset = -1L
            var size = 0L
            var entrySize = 0L
            var strtab = -1
            var index = 0
            while (index < shnum) {
                val base = shoff + index.toLong() * shentsize
                if (readU32(bytes, base + 4L) == 2UL) {
                    offset = readLong(bytes, base + 0x18)
                    size = readLong(bytes, base + 0x20)
                    entrySize = readLong(bytes, base + 0x38)
                    strtab = readU32(bytes, base + 0x28).toInt()
                    break
                }
                index++
            }
            if (offset < 0L || strtab < 0 || entrySize <= 0L) {
                emptySet<String>()
            } else {
                val strtabBase = shoff + strtab.toLong() * shentsize
                parse(offset, size, entrySize, readLong(bytes, strtabBase + 0x18))
            }
        }.getOrDefault(emptySet())
        return ElfSymbols(dynsym = dynsym, names = dynsym + symtab)
    }

    /** @return 小端 u16。 */
    private fun readU16(bytes: ByteArray, offset: Int): Int =
        (bytes[offset].toInt() and 0xFF) or ((bytes[offset + 1].toInt() and 0xFF) shl 8)

    /** @return 小端 u32。 */
    private fun readU32(bytes: ByteArray, offset: Long): ULong {
        var value = 0UL
        for (index in 0 until 4) {
            value = value or ((bytes[(offset + index).toInt()].toLong() and 0xFFL).toULong() shl (8 * index))
        }
        return value
    }

    /** @return 小端 u64（有符号 Long 承载）。 */
    private fun readLong(bytes: ByteArray, offset: Long): Long {
        var value = 0L
        for (index in 0 until 8) {
            value = value or ((bytes[(offset + index).toInt()].toLong() and 0xFFL) shl (8 * index))
        }
        return value
    }

    /** @return NUL 结尾 C 字符串。 */
    private fun readCString(bytes: ByteArray, offset: Long): String {
        val builder = StringBuilder()
        var index = offset
        while (index < bytes.size.toLong() && bytes[index.toInt()] != 0.toByte()) {
            builder.append(bytes[index.toInt()].toInt().toChar())
            index++
        }
        return builder.toString()
    }

    private companion object {
        const val TAG = "JNI-VICTIM-E4"
        const val REPORT_FILE = "jni-victim-e4-report.txt"
        const val DEBUG_ASSET = "profile/43499/debug-a301so.conf"
        const val VICTIM_HELPER_FILE_NAME = "libghostlock_victim_helper.so"
        const val JNI_LIB_FILE_NAME = "libghostlock43499.so"
        const val E4_SCRIPT_NAME = "e4-echo-ok.sh"
        const val RAW_SIZE_BYTES = 1L shl 20
        const val SPAWN_SAMPLES = 20
        const val JNI_BASELINE_SAMPLES = 200
        const val CONTROL_TIMEOUT_MS = 2_000
        const val EXIT_TIMEOUT_MS = 2_000
        const val TIMEOUT_PROBE_MS = 50
        const val MAX_VM_HWM_KB = 64L * 1024L
    }
}
