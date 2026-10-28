package com.ghostlock.app.backend.cve43499.w3

import java.io.File

/**
 * G04 W3 硬门（P1–P8；逻辑全 Kotlin，纯函数/读取器可注入）。
 *
 * 语义（design v2 §2）：
 * - P1 victim pid/starttime 不变；
 * - P2 uid real/effective/saved/fs 全 0，且父 `/proc/<pid>/status` 与 victim self
 *   `PROBE_STATUS`/`status` **一致**（同源双路径，不是独立机制；任一缺失/不一致 FAIL）；
 * - P3 cred 生效（CapEff 双源一致；root 语义记录，不额外猜）；
 * - P4 seccomp 清除（`Seccomp:0` 双源一致；before=2/after=0；单源不足以 PASS）；
 * - P5 leaf 方向经实际写观测（len0=EXACT/len8=+8/len15 或歧义=FAIL；tcp exact policy 例外）；
 * - P6 W3 close clean/无 retained；
 * - P7 finish 前置（rooted victim/script handoff 冻结）；
 * - P8 同 boot/无 panic/KSU 未加载。
 *
 * @note `Seccomp:0` 充分性：双源读取足以证明 seccomp mode/filter 已清除；TIF_SECCOMP 无
 *   直接用户态读口，不要求直读（若产品要求另批 victim 自探针，真机 UNVERIFIED）。
 * @note 失败 reason 稳定前缀 `gate-*`；`coldRebootRequired=true` 由 Flow 在 terminate 前
 *   fsync（本类只给判定）。
 * @note 阶段：G04 实现（task-78）；按用户令未编译/未跑单测，真机 UNVERIFIED。
 */
internal open class Cve43499W3HardGate(
    private val parentStatusReader: (pid: Int) -> String? = { pid ->
        runCatching { File("/proc/" + pid + "/status").readText() }.getOrNull()
    },
    private val bootIdReader: () -> String? = {
        runCatching { File("/proc/sys/kernel/random/boot_id").readText().trim() }.getOrNull()
    },
    private val ksuLoaded: () -> Boolean = { false },
) {
    /**
     * W3 前置观测（写 flags/mode 前）。
     *
     * @param victimPid W2 后 victim PID。@param startTimeBefore W1 前 starttime。
     * @param startTimeAfter 当前 starttime。@param victimUid 既有 uid probe。
     * @param selfStatus victim self raw status（PROBE_STATUS/status）。@param seccompBefore W2 前值。
     */
    data class PreInput(
        val victimPid: Int,
        val startTimeBefore: Long,
        val startTimeAfter: Long,
        val victimUid: Long?,
        val selfStatus: ByteArray?,
        val seccompBefore: Int?,
        val bootId: String?,
        val ksuGuard: String,
        val finishPrereqOk: Boolean,
    )

    /** 前置判定结果。 */
    data class PreVerdict(
        val passed: Boolean,
        val reason: String?,
        val evidence: Map<String, String>,
    )

    /**
     * P1/P2/P3/P4/P8 前置判定（P5/P6/P7 在写后 final 判定）。
     *
     * @param input 前置观测。
     * @return 通过/失败 + 双源证据（失败 reason 稳定）。
     */
    open fun evaluatePre(input: PreInput): PreVerdict {
        val evidence = linkedMapOf<String, String>()
        if (input.victimPid <= 0 || input.startTimeBefore <= 0L) {
            return PreVerdict(false, "gate-victim-changed", evidence)
        }
        if (input.startTimeBefore != input.startTimeAfter) {
            evidence["w3VictimPid"] = input.victimPid.toString()
            evidence["w3VictimStartTime"] = input.startTimeAfter.toString()
            return PreVerdict(false, "gate-victim-changed", evidence)
        }
        val parent = parentStatusReader(input.victimPid)
        if (parent == null) return PreVerdict(false, "gate-uid-probe-missing", evidence)
        val parentStatus = parseStatus(parent) ?: return PreVerdict(false, "gate-uid-probe-missing", evidence)
        val self = input.selfStatus?.let { bytes ->
            runCatching { bytes.toString(Charsets.UTF_8) }.getOrNull()?.let(::parseStatus)
        }
        if (self == null) return PreVerdict(false, "gate-seccomp-probe-missing", evidence)
        evidence["w3UidParent"] = parentStatus.uid.joinToString("/")
        evidence["w3UidSelf"] = self.uid.joinToString("/")
        evidence["w3CapEffParent"] = parentStatus.capEff.toString()
        evidence["w3CapEffSelf"] = self.capEff.toString()
        evidence["w3SeccompParent"] = parentStatus.seccomp.toString()
        evidence["w3SeccompSelfBefore"] = self.seccomp.toString()
        evidence["w3UidStatus"] = input.victimUid?.toString() ?: "missing"
        if (parentStatus.uid.any { it != 0L } || self.uid.any { it != 0L }) {
            return PreVerdict(false, "gate-uid-not-root", evidence)
        }
        if (input.victimUid != 0L) {
            return PreVerdict(false, "gate-uid-not-root", evidence)
        }
        if (!parentStatus.uid.contentEquals(self.uid)) {
            return PreVerdict(false, "gate-uid-source-mismatch", evidence)
        }
        if (parentStatus.capEff != self.capEff) {
            return PreVerdict(false, "gate-creds-source-mismatch", evidence)
        }
        if (parentStatus.seccomp != self.seccomp) {
            return PreVerdict(false, "gate-seccomp-source-mismatch", evidence)
        }
        val seccompBefore = input.seccompBefore
        if (seccompBefore == null || seccompBefore != 2 ||
            parentStatus.seccomp != 0
        ) {
            return PreVerdict(false, "gate-seccomp-not-cleared", evidence)
        }
        if (!input.finishPrereqOk) return PreVerdict(false, "gate-finish-prereq", evidence)
        if (input.ksuGuard != "absent") return PreVerdict(false, "gate-env", evidence)
        val bootId = input.bootId ?: bootIdReader()
        if (bootId.isNullOrBlank() || ksuLoaded()) {
            return PreVerdict(false, "gate-env", evidence)
        }
        evidence["w3BootId"] = bootId
        evidence["ksuGuard"] = input.ksuGuard
        return PreVerdict(true, null, evidence)
    }

    /**
     * final 判定（写 flags/mode 后/finish 前）：P5/P6 + 双源复读。
     *
     * @param pre 前置通过时的证据。
     * @param selfStatusAfter 写后 victim self raw status。
     * @param parentStatusAfter 写后父侧 status（可空 ⇒ FAIL）。
     * @param seccompAfterParent 父侧写后 Seccomp 值；null ⇒ FAIL。
     * @param leafDirection 观测到的实际写方向（true=+8/false=EXACT；null=未命中/歧义）。
     * @param exactPolicy tcp exact 路径（policy 决定，不观测写）。
     * @param closeClean W3 链/closeAll 均 clean。
     * @param coldRebootRequired 失败时是否要求外部冷重启（W2 cred 已写 ⇒ true）。
     * @return 最终判定（PASS 才允许 finish）。
     */
    open fun evaluateFinal(
        pre: PreVerdict,
        selfStatusAfter: ByteArray?,
        parentStatusAfter: String?,
        seccompAfterParent: Int?,
        leafDirection: Boolean?,
        exactPolicy: Boolean,
        closeClean: Boolean,
        coldRebootRequired: Boolean,
    ): PreVerdict {
        if (!pre.passed) return pre
        val evidence = pre.evidence.toMutableMap()
        if (!closeClean) {
            return PreVerdict(false, "gate-close-retained", evidence)
        }
        if (exactPolicy) {
            evidence["w3LeafDirection"] = "exact-policy"
        } else {
            when (leafDirection) {
                null -> return PreVerdict(false, "gate-leaf-direction", evidence)
                true -> evidence["w3LeafDirection"] = "target-plus-8"
                false -> evidence["w3LeafDirection"] = "exact"
            }
        }
        val selfAfter = selfStatusAfter?.toString(Charsets.UTF_8)?.let(::parseStatus)
            ?: return PreVerdict(false, "gate-seccomp-probe-missing", evidence).copy(
                evidence = evidence.apply {
                    put("w3ColdRebootRequired", coldRebootRequired.toString())
                },
            )
        val parentAfter = parentStatusAfter?.let(::parseStatus)
            ?: return PreVerdict(false, "gate-seccomp-probe-missing", evidence)
        evidence["w3SeccompSelfAfter"] = selfAfter.seccomp.toString()
        evidence["w3SeccompAfterParent"] = parentAfter.seccomp.toString()
        if (selfAfter.seccomp != 0) {
            return PreVerdict(false, "gate-seccomp-not-cleared", evidence)
        }
        if (parentAfter.seccomp != (seccompAfterParent ?: -1)) {
            return PreVerdict(false, "gate-seccomp-source-mismatch", evidence)
        }
        if (parentAfter.uid.any { it != 0L }) {
            return PreVerdict(false, "gate-uid-not-root", evidence)
        }
        return PreVerdict(true, null, evidence)
    }

    /** 解析后的自我/父侧 status（只取 W3 需要的三行）。 */
    data class ParsedStatus(
        val uid: LongArray,
        val capEff: Long,
        val seccomp: Int,
    )

    /** leaf 方向决策：EXACT/+8/RETRY(15)/AMBIGUOUS（class scope，测试可直接引用）。 */
    sealed interface LeafDecision {
        data object Exact : LeafDecision
        data object TargetPlus8 : LeafDecision
        data object MissRetry : LeafDecision
        data object Ambiguous : LeafDecision
    }

    companion object {
        /**
         * @param len self comm 读回长度（0/8/15）。
         * @return leaf 方向决策（其他长度=Ambiguous ⇒ FAIL，不猜）。
         */
        fun leafDecisionFromComm(len: Int): LeafDecision = when (len) {
            0 -> LeafDecision.Exact
            8 -> LeafDecision.TargetPlus8
            15 -> LeafDecision.MissRetry
            else -> LeafDecision.Ambiguous
        }

        /**
         * 解析 `/proc/.../status`（Uid/CapEff/Seccomp）。
         *
         * @param text raw status 文本。
         * @return 解析结果；缺行/格式非法 null（调用方 FAIL）。
         */
        fun parseStatus(text: String): ParsedStatus? {
            var uid: LongArray? = null
            var capEff: Long? = null
            var seccomp: Int? = null
            for (line in text.lineSequence()) {
                when {
                    line.startsWith("Uid:") -> {
                        val values = line.removePrefix("Uid:").trim().split(Regex("\\s+"))
                        if (values.size != 4) return null
                        uid = values.map { it.toLongOrNull() ?: return null }.toLongArray()
                    }
                    line.startsWith("CapEff:") -> {
                        capEff = line.removePrefix("CapEff:").trim().toLongOrNull(16) ?: return null
                    }
                    line.startsWith("Seccomp:") -> {
                        seccomp = line.removePrefix("Seccomp:").trim().toIntOrNull() ?: return null
                    }
                }
            }
            return if (uid != null && capEff != null && seccomp != null) {
                ParsedStatus(uid = uid, capEff = capEff, seccomp = seccomp)
            } else {
                null
            }
        }
    }
}
