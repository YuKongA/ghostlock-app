package com.ghostlock.app.backend.cve98260.markers

/**
 * helper stdout 的稳定 marker 解析（B2）。
 *
 * 契约来源：`app/src/main/cpp/gl98260/markers.h`（append-only）。**未知行忽略**；
 * 半行由调用方保留到下一批（本解析器只处理完整行）。解析结果是**事实快照**，
 * 不参与 allow/deny、不决定终态（终态由 app Flow 依据 store/verify 唯一决定）。
 *
 * @note marker 顺序：ENV → PREFLIGHT=ok → ARMED → SELINUX_OFF → PANIC_ON_WARN →
 *   FAKE_CRED → FAKE_UID → KSU_LOADED/KSU_MISSING → PARKED。ARMED 后任何失败 ⇒ 必须重启。
 */
internal object Cve98260Markers {
    /** 单行上限（超过则截断解析，仅保留前缀匹配）。 */
    internal const val MAX_LINE_BYTES: Int = 512

    /**
     * 累计事实快照（不可变；由 [parse] 生成）。
     *
     * @property envPresent 是否见到 `GLK98260_ENV`。
     * @property envPid helper 自报 pid；null=未回传/非法。
     * @property envUid helper 自报 uid；null=未回传/非法。
     * @property envDomain helper 自报 SELinux 域；null=未回传。
     * @property preflightOk 是否见到 `GLK98260_PREFLIGHT=ok`。
     * @property preflightFail 首个 `GLK98260_PREFLIGHT=fail:<reason>`；null=无。
     * @property armed 是否见到 `GLK98260_ARMED=1`。
     * @property selinuxOff 是否见到 `GLK98260_SELINUX_OFF=1`。
     * @property panicOnWarn `GLK98260_PANIC_ON_WARN=<n>` 的值；null=未见/非法。
     * @property fakeCred 是否见到 `GLK98260_FAKE_CRED=1`。
     * @property fakeUid 是否见到 `GLK98260_FAKE_UID=1`。
     * @property ksuLoaded 是否见到 `KSU_LOADED`。
     * @property ksuMissing 是否见到 `KSU_MISSING`。
     * @property parkedReason `GLK98260_PARKED=<reason>`；null=未 park。
     */
    internal data class Snapshot(
        val envPresent: Boolean = false,
        val envPid: Int? = null,
        val envUid: Int? = null,
        val envDomain: String? = null,
        val preflightOk: Boolean = false,
        val preflightFail: String? = null,
        val armed: Boolean = false,
        val selinuxOff: Boolean = false,
        val panicOnWarn: Int? = null,
        val fakeCred: Boolean = false,
        val fakeUid: Boolean = false,
        val ksuLoaded: Boolean = false,
        val ksuMissing: Boolean = false,
        val parkedReason: String? = null,
    ) {
        /** @return true=出现 park 证据或已在 ARMED 后失败（必须重启）。 */
        internal fun mustReboot(): Boolean = parkedReason != null

        /** @return true=达到 helper 侧成功候选（尚未独立验证）。 */
        internal fun helperSucceeded(): Boolean = ksuLoaded && parkedReason == null
    }

    /**
     * 解析一批完整日志行。
     *
     * @param lines 完整行（允许含未知行/空行）。
     * @param previous 上一批快照（半行上下文不在此处理）。
     * @return 累计快照；同字段重复出现时以最后一次非空值为准。
     */
    internal fun parse(lines: List<String>, previous: Snapshot = Snapshot()): Snapshot {
        var s = previous
        for (raw in lines) {
            val line = raw.take(MAX_LINE_BYTES)
            if (!line.startsWith("GLK98260_") && !line.contains("KSU_LOADED") &&
                !line.contains("KSU_MISSING") && !line.contains("parking without reap")
            ) {
                continue
            }
            s = when {
                line.startsWith("GLK98260_ENV ") -> parseEnv(line, s)
                line.startsWith("GLK98260_PREFLIGHT=fail:") ->
                    s.copy(preflightFail = line.substringAfter("fail:").trim())
                line.startsWith("GLK98260_PREFLIGHT=ok") -> s.copy(preflightOk = true)
                line.startsWith("GLK98260_ARMED=1") -> s.copy(armed = true)
                line.startsWith("GLK98260_SELINUX_OFF=1") -> s.copy(selinuxOff = true)
                line.startsWith("GLK98260_PANIC_ON_WARN=") ->
                    s.copy(panicOnWarn = parsePanic(line))
                line.startsWith("GLK98260_FAKE_CRED=1") -> s.copy(fakeCred = true)
                line.startsWith("GLK98260_FAKE_UID=1") -> s.copy(fakeUid = true)
                line.startsWith("GLK98260_PARKED=") ->
                    s.copy(parkedReason = line.substringAfter('=').trim().ifEmpty { "unknown" })
                line.contains("parking without reap") && s.parkedReason == null ->
                    s.copy(parkedReason = "parking-without-reap")
                line.contains("KSU_LOADED") -> s.copy(ksuLoaded = true)
                line.contains("KSU_MISSING") -> s.copy(ksuMissing = true)
                else -> s
            }
        }
        return s
    }

    /** 解析 `GLK98260_ENV pid=.. uid=.. domain=.. release=..` 中 app 需要的字段。 */
    private fun parseEnv(line: String, previous: Snapshot): Snapshot {
        val fields = line.substringAfter("GLK98260_ENV ").trim().split(' ')
        var pid: Int? = previous.envPid
        var uid: Int? = previous.envUid
        var domain: String? = previous.envDomain
        for (field in fields) {
            val kv = field.split('=', limit = 2)
            if (kv.size != 2) continue
            when (kv[0]) {
                "pid" -> pid = kv[1].toIntOrNull()
                "uid" -> uid = kv[1].toIntOrNull()
                "domain" -> domain = kv[1].takeIf { it.isNotBlank() }
            }
        }
        return previous.copy(envPresent = true, envPid = pid, envUid = uid, envDomain = domain)
    }

    /** @return panic_on_warn 值；非法/负数返回 null（调用方按"未知"处理）。 */
    private fun parsePanic(line: String): Int? =
        line.substringAfter("GLK98260_PANIC_ON_WARN=").trim().toIntOrNull()?.takeIf { it >= 0 }

    /** @return 需要 UI/状态机立即进入"必须重启"的事实（含 ARMED 后异常退出）。 */
    internal fun requiresReboot(snapshot: Snapshot, exitCode: Int?): Boolean {
        if (snapshot.mustReboot()) return true
        if (!snapshot.armed) return false
        if (snapshot.preflightFail != null) return true
        if (exitCode != null && exitCode != 0 && !snapshot.ksuLoaded) return true
        return false
    }
}
