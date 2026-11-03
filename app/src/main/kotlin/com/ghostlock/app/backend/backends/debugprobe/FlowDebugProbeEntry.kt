package com.ghostlock.app.backend.backends.debugprobe

import android.os.Build
import com.ghostlock.app.backend.host.FlowEntryParams
import com.ghostlock.app.core.flow.Flow
import com.ghostlock.app.core.flow.FlowEntry
import com.ghostlock.app.core.flow.FlowHostEnv
import com.ghostlock.app.core.flow.FlowRequest
import com.ghostlock.app.core.flow.FlowResult
import java.io.File
import java.io.FileOutputStream
import java.net.NetworkInterface
import java.nio.file.Files
import java.nio.file.attribute.PosixFilePermissions
import java.util.Collections
import java.util.Locale
import java.util.TimeZone
import kotlin.reflect.KClass

/** Debug Probe 的请求占位（入口只经反射调用）。 */
class DebugProbeRequest : FlowRequest

/**
 * Debug Probe 的 context-free Flow 入口（debug-only；纯只读环境探测）。
 *
 * 用途：把 host 进程可见的环境信息写入 Flow log 与 `host.log_path` 文件，
 * 供设备门禁验证「新进程通道 + log fd/落盘 + UI 回调」；**无任何攻击/写系统副作用**
 * （唯一写操作是 host.run_dir 下的本次 run 日志文件）。
 *
 * 输出行前缀：`debug-probe:host-*` / `build-*` / `prop:*` / `env:*` / `proc:*` /
 * `line:<0..127>` / `done`；读取失败记 `debug-probe:read-fail:<path>` 不抛异常。
 *
 * @note 仅 debug 构建经 [com.ghostlock.app.backend.backends.debugprobe.BackendDebugProbe] 注册；
 *   release 中本类可编译但不注册/不可选。
 * @note 日志内容可能含设备路径/属性；debug 专用，release 不运行。
 */
object FlowDebugProbeEntry : Flow<DebugProbeRequest> {
    override val backendClass: KClass<out Flow<*>> = FlowDebugProbeEntry::class

    override suspend fun run(request: DebugProbeRequest): FlowResult = failed(
        "probe:host-only",
    )

    override fun stop() = Unit

    /**
     * 执行一次 debug probe。
     *
     * @param env host 运行环境（params 含 `host.*`）。
     * @return Ok，evidence=`{debug_probe=ok, log_lines=128, host_log_path=<path>,
     *   debug_probe.persist_path=<run_dir 外路径>, persist_bytes=<n>}`。
     */
    @FlowEntry
    fun entry(env: FlowHostEnv): FlowResult {
        val values = env.params.values()
        val logPath = values[FlowEntryParams.KEY_HOST_LOG_PATH]?.toString() ?: ABSENT
        val lines = buildList {
            add("debug-probe:start")
            add("debug-probe:host-uid=" + (values[FlowEntryParams.KEY_HOST_UID]?.toString() ?: ABSENT))
            add("debug-probe:host-pid=" + (values[FlowEntryParams.KEY_HOST_PID]?.toString() ?: ABSENT))
            add("debug-probe:host-domain=" + (values[FlowEntryParams.KEY_HOST_DOMAIN]?.toString() ?: ABSENT))
            add("debug-probe:host-run-dir=" + (values[FlowEntryParams.KEY_HOST_RUN_DIR]?.toString() ?: ABSENT))
            add(
                "debug-probe:host-native-lib-dir=" +
                    (values[FlowEntryParams.KEY_HOST_NATIVE_LIB_DIR]?.toString() ?: ABSENT),
            )
            add("debug-probe:host-log-path=" + logPath)
            addAll(buildSnapshot())
            for (index in 0 until PROBE_LINE_COUNT) {
                add("debug-probe:line:$index")
            }
            add("debug-probe:done")
        }
        lines.forEach { line -> env.log(line) }
        appendToLogFile(logPath, lines)
        val persistPath = persistPathFor(runDir = values[FlowEntryParams.KEY_HOST_RUN_DIR]?.toString())
        val persistBytes = writePersistFile(persistPath, lines)
        if (persistPath != ABSENT && persistBytes == 0) {
            /* run_dir 外持久化失败：保留证据路径 + 具名标记，不抛异常。 */
            env.log("debug-probe:persist-fail:$persistPath")
        }
        return FlowResult.Ok(
            steps = emptyList(),
            evidence = linkedMapOf(
                "debug_probe" to "ok",
                "log_lines" to lines.size.toString(),
                "host_log_path" to logPath,
                "debug_probe.persist_path" to persistPath,
                "persist_bytes" to persistBytes.toString(),
            ),
        )
    }

    /** @return Build/System/proc 环境行（全部只读）。 */
    private fun buildSnapshot(): List<String> = buildList {
        add("debug-probe:build-display=" + Build.DISPLAY)
        add("debug-probe:build-device=" + Build.DEVICE)
        add("debug-probe:build-model=" + Build.MODEL)
        add("debug-probe:build-brand=" + Build.BRAND)
        add("debug-probe:build-manufacturer=" + Build.MANUFACTURER)
        add("debug-probe:build-fingerprint=" + Build.FINGERPRINT)
        add("debug-probe:build-sdk=" + Build.VERSION.SDK_INT)
        add("debug-probe:build-release=" + Build.VERSION.RELEASE)
        add("debug-probe:build-id=" + Build.ID)
        add("debug-probe:build-hardware=" + Build.HARDWARE)
        add("debug-probe:build-board=" + Build.BOARD)
        add("debug-probe:build-product=" + Build.PRODUCT)
        add("debug-probe:build-tags=" + Build.TAGS)
        add("debug-probe:build-type=" + Build.TYPE)
        add("debug-probe:build-time=" + Build.TIME)
        add("debug-probe:build-host=" + Build.HOST)
        add("debug-probe:build-user=" + Build.USER)
        add("debug-probe:build-incremental=" + Build.VERSION.INCREMENTAL)
        add("debug-probe:build-security-patch=" + Build.VERSION.SECURITY_PATCH)
        add("debug-probe:build-base-os=" + Build.VERSION.BASE_OS)
        add("debug-probe:build-codename=" + Build.VERSION.CODENAME)
        add("debug-probe:build-abi=" + Build.SUPPORTED_ABIS.joinToString(","))
        add("debug-probe:locale=" + Locale.getDefault().toString())
        add("debug-probe:timezone=" + TimeZone.getDefault().id)
        val runtime = Runtime.getRuntime()
        add("debug-probe:runtime-processors=" + runtime.availableProcessors())
        add("debug-probe:runtime-max-memory=" + runtime.maxMemory())
        add("debug-probe:runtime-total-memory=" + runtime.totalMemory())
        add("debug-probe:runtime-free-memory=" + runtime.freeMemory())
        add("debug-probe:time-millis=" + System.currentTimeMillis())
        add("debug-probe:time-nanos=" + System.nanoTime())
        System.getProperties().entries
            .map { entry -> entry.key.toString() to entry.value?.toString().orEmpty() }
            .sortedBy { it.first }
            .take(PROPERTY_LIMIT)
            .forEach { (key, value) ->
                add("debug-probe:prop:$key=" + sanitize(key, value))
            }
        System.getenv().entries
            .sortedBy { it.key }
            .take(PROPERTY_LIMIT)
            .forEach { (key, value) ->
                add("debug-probe:env:$key=" + sanitize(key, value))
            }
        add("debug-probe:proc-version=" + readFirstLine("/proc/version"))
        readLines("/proc/self/status", STATUS_LINES).forEachIndexed { index, line ->
            add("debug-probe:proc-status:$index=" + singleLine(line))
        }
        readLines("/proc/meminfo", MEMINFO_LINES).forEachIndexed { index, line ->
            add("debug-probe:meminfo:$index=" + singleLine(line))
        }
        readLines("/proc/cpuinfo", CPUINFO_LINES).forEachIndexed { index, line ->
            add("debug-probe:cpuinfo:$index=" + singleLine(line))
        }
        add("debug-probe:proc-cmdline=" + readFirstLine("/proc/cmdline"))
        add("debug-probe:proc-self-attr=" + readFirstLine("/proc/self/attr/current"))
        add("debug-probe:cpu-online=" + readFirstLine("/sys/devices/system/cpu/online"))
        add("debug-probe:cpu-present=" + readFirstLine("/sys/devices/system/cpu/present"))
        add("debug-probe:selinux-enforce=" + readFirstLine("/sys/fs/selinux/enforce"))
        add("debug-probe:boot-id=" + readFirstLine("/proc/sys/kernel/random/boot_id"))
        add("debug-probe:uptime=" + readFirstLine("/proc/uptime"))
        appendNetworkInterfaces()
    }

    /** 追加网络接口（name + 地址，最多 [NETWORK_LINES] 行；异常记 net-fail）。 */
    private fun MutableList<String>.appendNetworkInterfaces() {
        val networks = runCatching {
            Collections.list(NetworkInterface.getNetworkInterfaces())
        }.getOrElse { problem ->
            add("debug-probe:net-fail:" + problem::class.java.simpleName)
            return
        }
        networks.sortedBy { it.name }
            .take(NETWORK_LINES)
            .forEachIndexed { index, network ->
                val addresses = runCatching {
                    network.inetAddresses.toList()
                        .joinToString(",") { address -> address.hostAddress ?: "?" }
                }.getOrDefault("")
                add(
                    "debug-probe:net:$index:" + network.name + "=" +
                        network.isUp + ";" + singleLine(addresses),
                )
            }
    }

    /**
     * 计算 run_dir 之外的持久化路径：`<run_dir 父>/debug-probe/debug-probe-last.log`
     * （host 终态会 cleanup run_dir，因此「落盘」判据必须放在 run_dir 外）。
     *
     * @param runDir host 注入的 run_dir（可能缺失）。
     * @return 目标文件绝对路径；无法推导时 `<absent>`。
     */
    private fun persistPathFor(runDir: String?): String {
        if (runDir.isNullOrBlank() || runDir == ABSENT) return ABSENT
        val parent = File(runDir).parentFile ?: return ABSENT
        return File(File(parent, "debug-probe"), "debug-probe-last.log").absolutePath
    }

    /**
     * 写入 run_dir 外持久化文件（覆盖写，best-effort，0600）。
     *
     * @param persistPath 目标路径（`<absent>` 时跳过）。
     * @param lines 日志行。
     * @return 写入字节数；失败 0（不抛）。
     */
    private fun writePersistFile(persistPath: String, lines: List<String>): Int {
        if (persistPath == ABSENT || persistPath.isBlank()) return 0
        return runCatching {
            val file = File(persistPath)
            file.parentFile?.mkdirs()
            var bytes = 0
            FileOutputStream(file, false).use { stream ->
                lines.forEach { line ->
                    val encoded = line.toByteArray(Charsets.UTF_8)
                    stream.write(encoded)
                    stream.write('\n'.code)
                    bytes += encoded.size + 1
                }
                stream.flush()
            }
            runCatching {
                Files.setPosixFilePermissions(
                    file.toPath(),
                    PosixFilePermissions.fromString("rw-------"),
                )
            }
            bytes
        }.getOrDefault(0)
    }

    /**
     * 把日志行写入 host.log_path（best-effort；失败记录但不抛）。
     *
     * @param logPath host 注入的日志文件路径（`<absent>` 时跳过）。
     * @param lines 已记录的行。
     */
    private fun appendToLogFile(logPath: String, lines: List<String>) {
        if (logPath == ABSENT || logPath.isBlank()) return
        runCatching {
            FileOutputStream(File(logPath), true).use { stream ->
                lines.forEach { line ->
                    stream.write(line.toByteArray(Charsets.UTF_8))
                    stream.write('\n'.code)
                }
                stream.flush()
            }
            runCatching {
                Files.setPosixFilePermissions(
                    File(logPath).toPath(),
                    PosixFilePermissions.fromString("rw-------"),
                )
            }
        }.onFailure { problem ->
            /* 写入失败不改变探测结论；host 侧仍有内存 log/callback。 */
            println("debug-probe:write-fail:$logPath:" + problem::class.java.simpleName)
        }
    }

    /** @return 文件首行（去空白）；失败 `debug-probe:read-fail:<path>`。 */
    private fun readFirstLine(path: String): String = runCatching {
        File(path).bufferedReader().use { it.readLine().orEmpty().trim() }
    }.getOrElse { READ_FAIL_PREFIX + path }

    /** @return 文件前 [limit] 行；失败单行标记。 */
    private fun readLines(path: String, limit: Int): List<String> = runCatching {
        File(path).bufferedReader().use { reader ->
            buildList {
                repeat(limit) {
                    val line = reader.readLine() ?: return@buildList
                    add(line.trim().take(MAX_VALUE_LEN))
                }
            }
        }
    }.getOrElse { listOf(READ_FAIL_PREFIX + path) }

    /** @return 单行化（去换行/截断）。 */
    private fun singleLine(value: String): String =
        value.replace('\n', ' ').replace('\r', ' ').take(MAX_VALUE_LEN)

    /** @return 敏感键脱敏后的单行值。 */
    private fun sanitize(key: String, value: String): String {
        val lower = key.lowercase()
        if (SENSITIVE_KEYS.any { lower.contains(it) }) return "<redacted>"
        return singleLine(value)
    }

    /** @return 具名失败。 */
    private fun failed(reason: String): FlowResult = FlowResult.Failed(
        reason = reason,
        failedStep = null,
        steps = emptyList(),
        evidence = emptyMap(),
    )

    /** 探针固定行数（debug-probe:line:0..127）。 */
    private const val PROBE_LINE_COUNT = 128

    /** /proc/meminfo 摘要行数。 */
    private const val MEMINFO_LINES = 8

    /** /proc/cpuinfo 摘要行数。 */
    private const val CPUINFO_LINES = 6

    /** 网络接口摘要上限。 */
    private const val NETWORK_LINES = 12

    /** properties/env 摘要上限。 */
    private const val PROPERTY_LIMIT = 40

    /** /proc/self/status 行数。 */
    private const val STATUS_LINES = 12

    /** 单行值最大长度。 */
    private const val MAX_VALUE_LEN = 400

    /** 缺失 host 参数占位。 */
    private const val ABSENT = "<absent>"

    /** 读取失败行前缀。 */
    private const val READ_FAIL_PREFIX = "debug-probe:read-fail:"

    /** 敏感键（日志脱敏）。 */
    private val SENSITIVE_KEYS = listOf("key", "token", "password", "secret", "credential")
}
