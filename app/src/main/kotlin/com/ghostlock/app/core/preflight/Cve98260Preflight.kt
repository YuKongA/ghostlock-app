package com.ghostlock.app.core.preflight

/**
 * CVE-2026-98260 预检求值器（B2；**单一纯函数**）。
 *
 * 同一份规则同时服务两处（N-06/N-29）：
 * - app `Flow98260Impl.validate`：app 可见事实（profile/Shizuku/store/boot_id）；
 * - service `Cve98260RemoteExecutor.prepare`：shell 域事实（F1–F8/F11/F12）。
 * app 不读 `/proc/modules`；service 读不到 app prefs（F9 只在 app 侧求值）。
 *
 * 本对象无 IO、无 Android 依赖、无副作用；事实由调用方收集后传入。
 */
internal object Cve98260Preflight {
    /**
     * app 初筛事实（F9/F10/F12）。
     *
     * @note 执行体（uid/模式）由 UI 按 `BackendSpec.uidRequirement` 检查；backend 不自检。
     */
    internal data class AppFacts(
        val profileParsed: Boolean,
        val releaseMatchesDevice: Boolean,
        val payloadReady: Boolean,
        val storeBlocked: Boolean,
        val bootIdKnown: Boolean,
    )

    /** service 完整预检事实（F1–F8/F10–F12；F9 由 app 判定后传入布尔记录）。 */
    internal data class ServiceFacts(
        val selfUid: Int,
        val selfDomain: String,
        val tracefsWritable: Boolean,
        val slabOrderActual: Long?,
        val slabOrderExpected: Long,
        val pageSizeActual: Long,
        val pageSizeExpected: Long,
        val cpuCount: Int,
        val cpuExpected: Long,
        val ksuLoaded: Boolean?,
        val managerLibksudPresent: Boolean,
        val bootId: String?,
        val journalState: JournalState,
        val journalBootId: String?,
        val currentBootId: String?,
        val appStoreBlocked: Boolean,
    )

    /** service 侧 journal（`/data/local/tmp/glk98260.run`）状态。 */
    internal enum class JournalState { ABSENT, INSTALLED, LAUNCHED, ARMED, UNKNOWN, READ_FAILED }

    /** 求值结果。 */
    internal data class Result(val ok: Boolean, val reasons: List<String>) {
        companion object {
            /** @return 全通过结果。 */
            internal val OK: Result = Result(true, emptyList())
        }
    }

    /**
     * app 初筛。
     *
     * @param facts app 可见事实。
     * @return ok=false 时 reasons 为具名 `cve98260:preflight:*`（全部保留，不吞）。
     */
    internal fun evaluateApp(facts: AppFacts): Result {
        val reasons = mutableListOf<String>()
        if (!facts.profileParsed) reasons += "cve98260:preflight:profile-missing"
        if (!facts.payloadReady) reasons += "cve98260:preflight:payload-missing"
        if (!facts.releaseMatchesDevice) reasons += "cve98260:preflight:release-mismatch"
        if (facts.storeBlocked) reasons += "cve98260:preflight:must-reboot"
        if (!facts.bootIdKnown) reasons += "cve98260:preflight:boot-id-unknown"
        return Result(reasons.isEmpty(), reasons)
    }

    /**
     * service 完整预检。
     *
     * @param facts shell 域事实 + app 已判定的 F9 布尔（仅作证据记录）。
     * @return ok=false 时 reasons 为具名 `cve98260:preflight:*`。
     */
    internal fun evaluateService(facts: ServiceFacts): Result {
        val reasons = mutableListOf<String>()
        if (facts.selfUid != 2000) reasons += "cve98260:preflight:shell-domain:uid"
        if (!facts.selfDomain.contains("u:r:shell:s0")) {
            reasons += "cve98260:preflight:shell-domain:context"
        }
        if (!facts.tracefsWritable) reasons += "cve98260:preflight:tracefs-not-writable"
        if (facts.slabOrderActual != facts.slabOrderExpected) {
            reasons += "cve98260:preflight:slab-order:" +
                (facts.slabOrderActual?.toString() ?: "unreadable")
        }
        if (facts.pageSizeActual != facts.pageSizeExpected) {
            reasons += "cve98260:preflight:page-size:" + facts.pageSizeActual
        }
        if (facts.cpuCount <= facts.cpuExpected.toInt()) {
            reasons += "cve98260:preflight:cpu-count:" + facts.cpuCount
        }
        if (facts.ksuLoaded == null) {
            reasons += "cve98260:preflight:modules-unreadable"
        } else if (facts.ksuLoaded) {
            reasons += "cve98260:preflight:ksu-loaded"
        }
        if (!facts.managerLibksudPresent) reasons += "cve98260:preflight:manager-missing"
        if (facts.bootId.isNullOrBlank()) reasons += "cve98260:preflight:boot-id-unknown"
        if (journalBlocked(facts)) reasons += "cve98260:preflight:must-reboot"
        if (facts.appStoreBlocked) reasons += "cve98260:preflight:must-reboot"
        return Result(reasons.isEmpty(), reasons)
    }

    /**
     * journal 安全闸判定（F11）。
     *
     * @param facts service 事实。
     * @return true=本 boot 已被某次未终结运行占用，必须 Refused。
     * @note `installed` 且 boot 匹配 ⇒ 允许重装（上次停在写 payload 后、未 spawn）；
     *   `launched/armed/unknown/read_failed` 且 boot 匹配 ⇒ 阻止。
     */
    internal fun journalBlocked(facts: ServiceFacts): Boolean {
        if (facts.journalState == JournalState.ABSENT) return false
        if (facts.journalState == JournalState.READ_FAILED) return true
        val sameBoot = facts.journalBootId != null && facts.currentBootId != null &&
            facts.journalBootId == facts.currentBootId
        if (!sameBoot) return false
        return when (facts.journalState) {
            JournalState.INSTALLED -> false
            JournalState.LAUNCHED, JournalState.ARMED, JournalState.UNKNOWN -> true
            JournalState.ABSENT, JournalState.READ_FAILED -> true
        }
    }
}
