package com.ghostlock.app.backend.cve43499.finish

import java.io.File

/**
 * 验收所需的只读证据源 seam：只读文件存在性与字节。
 *
 * @note 生产实现是 [AndroidFinishEvidenceSource]；宿主验证可注入 fake（例如临时目录）。
 * @note 本 seam **不**执行命令、不解析 terminal 输出：验收只认文件/脚本对象证据。
 */
internal interface FinishEvidenceSource {
    /**
     * @param path 绝对路径。
     * @return true 表示路径存在（文件或目录）。
     */
    fun exists(path: String): Boolean

    /**
     * @param path 绝对路径。
     * @return 文件字节；不存在/不可读/非普通文件返回 null。
     */
    fun readBytes(path: String): ByteArray?
}

/**
 * 生产证据源：直接读 Android 文件系统（root 侧路径在 app 域读不到时返回 null）。
 *
 * @note 读失败一律按「证据缺失」处理，不抛异常、不猜测。
 * @note 阶段：G4L-6 finish 验收。
 */
internal object AndroidFinishEvidenceSource : FinishEvidenceSource {
    override fun exists(path: String): Boolean =
        runCatching { File(path).exists() }.getOrDefault(false)

    override fun readBytes(path: String): ByteArray? = runCatching {
        val file = File(path)
        if (!file.isFile) null else file.readBytes()
    }.getOrNull()
}

/**
 * 一次收尾验收输入：全部为可注入事实，不接受「terminal 输出」这类不可核验证据。
 *
 * @property expectedUid 期望的 victim uid；默认 root（0）。
 * @property observedUid victim 探测读回的 uid；null 表示没有证据。
 * @property handoff UI 生成的脚本冻结对象；null 表示交接缺失。
 * @property resourcesReleased 全部资源已终结的证据（由 run 编排按 close 回执计算）。
 * @property source 文件证据源。
 */
internal data class FinishAcceptanceInput(
    /** 本次运行 id；marker 内容、执行证据与 victim 绑定都必须与它一致。 */
    val runId: String,
    val expectedUid: Long = FinishAcceptance.ROOT_UID,
    val observedUid: Long?,
    val handoff: FinishHandoff?,
    val resourcesReleased: Boolean?,
    /** 收尾脚本的实际执行证据（exit/超时/runId）；缺失即拒绝。 */
    val execution: FinishExecutionEvidence?,
    /** victim ↔ Actor 绑定证据（PID/starttime/地址）；缺失即拒绝。 */
    val binding: RunBindingEvidence?,
    val source: FinishEvidenceSource = AndroidFinishEvidenceSource,
    /** ksud 证据的有界命令 seam（只取退出码 + 输出摘要）。 */
    val commandSource: FinishCommandSource = ProcessBuilderFinishCommandSource,
)

/** 收尾验收结论；成功必须全部证据齐备，否则列出全部具名缺失原因。 */
internal sealed interface FinishVerdict {
    /** 全部证据齐备且互相一致。 */
    data object Accepted : FinishVerdict

    /** 具名拒绝：reasons 非空、稳定可检索、按固定顺序排列。 */
    data class Refused(val reasons: List<String>) : FinishVerdict
}

/**
 * 收尾成功验收：uid / 脚本对象身份 / ksud / marker / SELinux 恢复 / 资源终结。
 *
 * 成功判据（缺一即具名拒绝，**不伪成功**）：
 * 1. victim uid == [ROOT_UID]；
 * 2. 脚本对象存在，且重新计算的 SHA-256 与 handoff 冻结值一致（对拍同一个脚本对象）；
 * 3. root 管理器（ksud/apd）证据存在；
 * 4. 收尾成功 marker 存在，且失败 marker 不存在；
 * 5. SELinux 已恢复 Enforcing；
 * 6. 调用方给出「完整所有权链已终结」的正向证据。
 *
 * @note 明确不用 terminal 输出、日志文本或 `exit` 码作为证据；日志只做事后审计。
 * @note 阶段：G4L-6 finish；真机门禁仍需覆盖同一组判据。
 */
internal object FinishAcceptance {
    /** 期望的 root uid。 */
    const val ROOT_UID: Long = 0L

    /** root 管理器（ksud / KernelPatch 的 apd）候选路径；任一存在即视为证据。 */
    val KSUD_PATH_CANDIDATES: List<String> = listOf(
        "/data/adb/ksu/bin/ksud",
        "/data/adb/ksud",
        "/data/adb/ksu/bin/ksud.apk",
        "/data/adb/apd",
    )

    /** 收尾成功 marker：脚本 `STATUS==0` 分支写入。 */
    const val SUCCESS_MARKER_PATH: String = "/dev/dfm6"

    /** 收尾失败 marker：脚本 `STATUS!=0` 分支写入。 */
    const val FAILURE_MARKER_PATH: String = "/dev/dfme2"

    /** SELinux enforce 状态文件；内容 `1` 表示 Enforcing。 */
    const val SELINUX_ENFORCE_PATH: String = "/sys/fs/selinux/enforce"

    /** ksud 版本查询参数（有界执行，只取退出码 + 输出摘要）。 */
    const val KSUD_VERSION_ARGUMENT: String = "--version"

    /** ksud 证据命令的有界超时（毫秒）。 */
    const val KSUD_COMMAND_TIMEOUT_MS: Long = 5_000L

    /**
     * 对一次收尾做具名验收。
     *
     * @param input 全部可注入证据。
     * @return [FinishVerdict.Accepted] 或 [FinishVerdict.Refused]（列出全部缺失原因）。
     * @note 本函数不抛异常：证据源抛错按「证据缺失」处理。
     */
    internal fun evaluate(input: FinishAcceptanceInput): FinishVerdict {
        val reasons = ArrayList<String>()

        /* 0. runId：必须显式给出；所有运行内证据都以它为绑定键。 */
        if (input.runId.isBlank()) reasons.add("finish-run-id-missing")

        /* 1. uid 证据：必须是 root。 */
        val observedUid = input.observedUid
        when {
            observedUid == null -> reasons.add("finish-uid-missing")
            observedUid != input.expectedUid ->
                reasons.add("finish-uid-not-root:" + observedUid)
        }

        /* 2. 脚本对象身份：路径存在且重新计算的 SHA-256 与冻结值一致。 */
        val handoff = input.handoff
        if (handoff == null) {
            reasons.add("finish-handoff-missing")
        } else {
            val scriptBytes = readBytesOrNull(input.source, handoff.scriptPath)
            if (scriptBytes == null) {
                reasons.add("finish-script-missing:" + handoff.scriptPath)
            } else {
                if (scriptBytes.size != handoff.scriptSizeBytes) {
                    reasons.add("finish-script-size-mismatch")
                }
                val observedSha = runCatching { FinishHandoff.sha256Hex(scriptBytes) }
                    .getOrNull()
                if (observedSha == null || observedSha != handoff.scriptSha256Hex) {
                    reasons.add("finish-script-sha-mismatch")
                }
            }
        }

        /* 3. root 管理器（ksud/apd）证据：路径存在 + 有界命令的退出码/输出摘要。 */
        val ksudPath = KSUD_PATH_CANDIDATES.firstOrNull { path ->
            runCatching { input.source.exists(path) }.getOrDefault(false)
        }
        if (ksudPath == null) {
            reasons.add("finish-ksud-missing")
        } else {
            val result = runCatching {
                input.commandSource.run(
                    listOf(ksudPath, KSUD_VERSION_ARGUMENT),
                    KSUD_COMMAND_TIMEOUT_MS,
                )
            }.getOrNull()
            val proven = result != null && !result.timedOut &&
                result.exitCode == 0 && !result.outputDigestSha256.isNullOrBlank()
            if (!proven) reasons.add("ksud-unproven")
        }

        /* 4. marker 证据：成功 marker 内容必须 == 本次 runId；失败 marker 不得存在。 */
        val successMarkerText = readTextOrNull(input.source, SUCCESS_MARKER_PATH)?.trim()
        when {
            successMarkerText == null -> reasons.add("finish-marker-missing")
            input.runId.isNotBlank() && successMarkerText != input.runId ->
                reasons.add("finish-marker-run-id-mismatch:" + successMarkerText.take(64))
        }
        val failureMarkerText = readTextOrNull(input.source, FAILURE_MARKER_PATH)?.trim()
        if (!failureMarkerText.isNullOrEmpty()) {
            reasons.add("finish-marker-failure-present:" + failureMarkerText.take(64))
        }

        /* 5. SELinux 恢复证据：内容必须是 1（Enforcing）。 */
        val enforce = readTextOrNull(input.source, SELINUX_ENFORCE_PATH)?.trim()
        when {
            enforce == null -> reasons.add("finish-selinux-missing")
            enforce != "1" -> reasons.add("finish-selinux-not-enforcing:" + enforce)
        }

        /* 6. 收尾脚本实际执行证据：exit==0、未超时、runId 一致。 */
        val execution = input.execution
        if (execution == null) {
            reasons.add("finish-execution-missing")
        } else {
            if (execution.timedOut) reasons.add("finish-execution-timeout")
            when (val exitCode = execution.exitCode) {
                null -> reasons.add("finish-execution-exit-missing")
                0 -> Unit
                else -> reasons.add("finish-execution-exit:" + exitCode)
            }
            if (execution.runId != input.runId) {
                reasons.add("finish-execution-run-id-mismatch")
            }
            /* 执行链退出后读回的 marker 也必须与 runId 一致。 */
            val executionMarker = execution.successMarkerContent?.trim()
            if (!executionMarker.isNullOrEmpty() && executionMarker != input.runId) {
                reasons.add("finish-execution-marker-run-id-mismatch")
            }
            if (!execution.failureMarkerContent.isNullOrEmpty()) {
                reasons.add("finish-execution-marker-failure-present")
            }
        }

        /* 7. victim↔Actor 绑定证据：身份/runId/地址/starttime 必须一致。 */
        val binding = input.binding
        if (binding == null) {
            reasons.add("finish-binding-missing")
        } else {
            if (binding.runId != input.runId) {
                reasons.add("finish-binding-run-id-mismatch")
            }
            val victim = binding.victim
            if (victim.pid <= 0) reasons.add("finish-binding-pid-missing")
            if (victim.startTimeTicks <= 0L) reasons.add("finish-binding-starttime-missing")
            if (victim.uid != input.expectedUid) {
                reasons.add("finish-binding-uid-mismatch:" + victim.uid)
            }
            if (binding.victimStartTimeAfterExecute != victim.startTimeTicks) {
                reasons.add("finish-binding-starttime-changed")
            }
            if (binding.primitiveWaiterAddress == 0L ||
                binding.primitiveTaskAddress == 0L ||
                binding.primitiveLockAddress == 0L
            ) {
                reasons.add("finish-binding-address-missing")
            }
        }

        /* 8. 资源终结证据：必须由调用方给出正向证据，缺失同样拒绝。 */
        if (input.resourcesReleased != true) {
            reasons.add("finish-resources-not-released")
        }

        return if (reasons.isEmpty()) {
            FinishVerdict.Accepted
        } else {
            FinishVerdict.Refused(reasons)
        }
    }

    /**
     * @param source 证据源。
     * @param path 文件路径。
     * @return 文件字节；读失败返回 null（按证据缺失处理）。
     */
    private fun readBytesOrNull(source: FinishEvidenceSource, path: String): ByteArray? =
        runCatching { source.readBytes(path) }.getOrNull()

    /**
     * @param source 证据源。
     * @param path 文件路径。
     * @return 文本内容；读失败返回 null（按证据缺失处理）。
     */
    private fun readTextOrNull(source: FinishEvidenceSource, path: String): String? =
        runCatching { source.readBytes(path)?.decodeToString() }.getOrNull()
}
