package com.ghostlock.app.backend.cve98260.finish

import com.ghostlock.app.backend.cve98260.profile.Cve98260RunId

/**
 * 98260 收尾执行/验收证据（R1d；本地中性 DTO）。
 *
 * 与 43499 的 `FinishExecutionEvidence`/`FinishAcceptance` **不共享类型/字段**：98260 没有
 * waiter/task/lock primitive 地址，也没有 43499 的 binding/resourcesReleased 合同；本 DTO 只承载
 * service 侧可核验的事实（脚本 sha、marker、uid、SELinux、ksud、exit）。缺失即拒绝，不猜。
 *
 * @property runId 本次 run id（marker 内容必须与它一致）。
 * @property scriptSha256 service 重算的脚本 sha256（app 侧与生成值对拍）。
 * @property expectedScriptSha256 app 侧生成脚本的 sha256（验收输入）。
 * @property exitCode worker 报告的脚本退出码（null=未报告）。
 * @property timedOut 是否超时；98260 payload **不对 finish 脚本施加 kill 超时**，出现 true 即拒绝。
 * @property observedUid worker 报告的 uid（应为 0）。
 * @property selinuxEnforcing `/sys/fs/selinux/enforce` 读回（UI `restoreSelinux` 开启时强制）。
 * @property ksudPresent 管理器/ksud 证据存在（service 读）。
 */
internal data class Cve98260FinishEvidence(
    val runId: String,
    val scriptSha256: String,
    val expectedScriptSha256: String,
    val scriptPresent: Boolean,
    val exitCode: Int?,
    val timedOut: Boolean,
    val observedUid: Long?,
    val selinuxEnforcing: Boolean?,
    val ksudPresent: Boolean?,
    /** UI finish 配置是否要求把 SELinux 恢复 Enforcing（`FinishState.restoreSelinux`）。 */
    val requireSelinuxEnforcing: Boolean = false,
)

/**
 * 98260 收尾验收（R1d；独立于 43499 `FinishAcceptance`）。
 *
 * 成功判据（缺一即具名拒绝，不伪成功）：
 * 1. runId 非空且格式合法；
 * 2. 脚本存在且 service 重算 sha256 == app 生成脚本 sha256；
 * 3. worker 报告 exit==0 且 timedOut=false；
 * 4. uid 证据==0；
 * 5. ksud/管理器证据存在；
 * 6. 若 UI 配置 `restoreSelinux=true`，SELinux enforce==1。
 *
 * @note 不使用 terminal/日志文本作为成功证据（日志只做事后审计）。
 */
internal object Cve98260FinishAcceptance {
    /**
     * @param evidence service 回传证据。
     * @return [Verdict.Accepted] 或 [Verdict.Refused]（按固定顺序列出全部缺失原因）。
     */
    internal fun evaluate(evidence: Cve98260FinishEvidence): Verdict {
        val reasons = mutableListOf<String>()
        if (!Cve98260RunId.valid(evidence.runId)) reasons += "cve98260-finish:run-id"
        if (!evidence.scriptPresent) reasons += "cve98260-finish:script-missing"
        if (evidence.scriptSha256.isBlank()) {
            reasons += "cve98260-finish:script-sha-missing"
        } else if (evidence.scriptSha256 != evidence.expectedScriptSha256) {
            reasons += "cve98260-finish:script-sha-mismatch"
        }
        if (evidence.timedOut) reasons += "cve98260-finish:timeout"
        if (evidence.exitCode != 0) {
            reasons += "cve98260-finish:exit:" + (evidence.exitCode?.toString() ?: "missing")
        }
        if (evidence.observedUid != 0L) {
            reasons += "cve98260-finish:uid:" + (evidence.observedUid?.toString() ?: "missing")
        }
        if (evidence.ksudPresent != true) reasons += "cve98260-finish:ksud-missing"
        if (evidence.requireSelinuxEnforcing && evidence.selinuxEnforcing != true) {
            reasons += "cve98260-finish:selinux"
        }
        return if (reasons.isEmpty()) Verdict.Accepted else Verdict.Refused(reasons)
    }

    /** 验收结论。 */
    internal sealed interface Verdict {
        /** 全部证据齐备且一致。 */
        data object Accepted : Verdict

        /** 具名拒绝（稳定可检索）。 */
        data class Refused(val reasons: List<String>) : Verdict
    }
}
