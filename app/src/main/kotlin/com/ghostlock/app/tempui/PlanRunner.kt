package com.ghostlock.app.tempui

import com.ghostlock.app.core.AccessResult
import com.ghostlock.app.core.AccessSessionRequest
import com.ghostlock.app.core.steps.PlanCatalog
import com.ghostlock.app.core.steps.StepEngine
import com.ghostlock.app.core.steps.StepId
import com.ghostlock.app.core.steps.StepInputs
import com.ghostlock.app.core.steps.StepOutcome
import com.ghostlock.app.core.steps.StepResult
import com.ghostlock.app.data.nativeaccess.JniAccessSession
import com.ghostlock.app.data.nativeaccess.JniAccessSessionFactory
import com.ghostlock.app.data.nativeaccess.JniOpsInvoker
import com.ghostlock.app.data.nativeaccess.NativeProtocol
import com.ghostlock.app.data.profile.AssetLister
import com.ghostlock.app.data.profile.AssetSource
import com.ghostlock.app.data.profile.DeviceProfile
import com.ghostlock.app.log.GhostLog

/**
 * 跑一遍**这台机器的画像声明的**计划，并把结局交出去（J8）。
 *
 * 与 J6 那版的三点不同，正是这一批的内容：
 *  1. 步骤表来自 profile（available.<backend>.queue[].step），不再是代码里的常量；
 *  2. 输入不再用占位地址 —— 画像给的是**镜像偏移**，真正的内核地址要等运行期发现（J3b），
 *     所以没有地址时每一步**指名**拒绝（precondition:w1-address），而不是拿一个假地址去动内核；
 *  3. 结局是一个 PlanOutcome：画像名、从画像读到的数字、每步结果、会话终态与清理状态 ——
 *     交给界面与日志，以后交给真正的终端交接。
 *
 * 结论都写进 logcat（TAG=GhostlockPlan），设备门禁按这些行做机器校验。
 */
object PlanRunner {
    /** 计划日志的 logcat 标签。 */
    const val TAG = "GhostlockPlan"

    /** 本批要跑的 backend（顺序固定）。 */
    private val BACKENDS = listOf("cve_2026_43499", "cve_2026_43284")

    /**
     * 一次计划的结局。
     *
     * @property profileAsset 命中的画像文件名（选错档就是要在这里看出来）。
     * @property profileNumbers 从画像里读到的数字（按名字列出），让证据里能对拍。
     * @property steps 每一步的结果（顺序即执行顺序）。
     * @property terminalPhase 跑完后会话自己报的相位。
     * @property cleanup 跑完后会话的清理状态（不确定效果会留 DIRTY）。
     */
    data class PlanOutcome(
        val backendId: String,
        val profileAsset: String,
        val profileNumbers: List<Pair<String, Long>>,
        val steps: List<StepResult>,
        val terminalPhase: String?,
        val cleanup: String?,
    ) {
        /** 第一处拒绝（有则说明计划停在哪一步、为什么）。 */
        val refusal: StepResult?
            get() = steps.firstOrNull { it.outcome == StepOutcome.Refused }

        val frozen: StepResult?
            get() = steps.firstOrNull { it.outcome == StepOutcome.Frozen }

        /** 一行总账：谁、用哪份画像、声明了哪些步、跑到哪、停在哪、终态是什么。 */
        fun summary(): String {
            val stop = refusal ?: frozen
            return "plan: " + backendId +
                " profile=" + profileAsset +
                " declared=" + steps.joinToString(",") { it.step.token } +
                " ran=" + steps.count { it.outcome == StepOutcome.Ran } +
                if (stop == null) {
                    " outcome=complete"
                } else {
                    " stopped=" + stop.step.token + ":" + stop.outcome + ":" + stop.reason
                } +
                " phase=" + (terminalPhase ?: "unknown") +
                " cleanup=" + (cleanup ?: "unknown")
        }
    }

    /**
     * 跑这台机器声明的两个 backend 的计划。
     *
     * @param release 内核 release（真机取 System.getProperty("os.version")）。
     * @return 每个 backend 的结局；画像装不上时为**空表**，理由已写进日志。
     */
    fun run(
        release: String,
        assets: AssetSource,
        lister: AssetLister,
        factory: JniAccessSessionFactory = JniAccessSessionFactory(),
    ): List<PlanOutcome> {
        val profile = DeviceProfile.load(release, assets, lister).getOrElse { failed ->
            line("plan: profile unusable (" + (failed.message ?: "unknown") + ")")
            return emptyList()
        }
        line(
            "plan: profile=" + profile.assetName + " release=" + profile.release +
                " generic=" + profile.generic + " schema=" + profile.schemaVersion(),
        )

        val outcomes = mutableListOf<PlanOutcome>()
        for (backend in BACKENDS) {
            if (!profile.declares(backend)) {
                line("plan: " + backend + " not declared by this profile")
                continue
            }
            val (steps, refusal) = PlanCatalog.fromTokens(profile.declaredStepNames(backend))
            if (refusal != null) {
                line("plan: " + backend + " refused by its own declaration (" + refusal + ")")
                continue
            }
            val numbers = profileNumbers(profile, backend)
            line(
                "plan: " + backend + " declared=" + steps.joinToString(",") { it.token } +
                    " route=" + (profile.route(backend) ?: "none") +
                    " numbers=" + numbers.joinToString(",") { it.first + "=" + it.second },
            )
            outcomes += runBackend(backend, steps, numbers, profile.assetName, factory)
        }
        return outcomes
    }

    /**
     * 画像给这一步提供的数字（今天只有 43499 有）：它们是**镜像偏移**，不是地址。
     *
     * 列出来是为了让证据里看得见画像被真的读到了，以及让 J3b 的发现步骤知道该往哪儿加基址。
     */
    private fun profileNumbers(profile: DeviceProfile, backend: String): List<Pair<String, Long>> {
        if (backend != "cve_2026_43499") {
            return emptyList()
        }
        val names = listOf("selinux_enforcing", "security_hook_heads", "init_task", "init_cred")
        return names.mapNotNull { name -> profile.imageOffset(backend, name)?.let { name to it } }
    }

    private fun runBackend(
        backend: String,
        steps: List<StepId>,
        numbers: List<Pair<String, Long>>,
        profileAsset: String,
        factory: JniAccessSessionFactory,
    ): PlanOutcome {
        /* 输入里**没有地址**：镜像偏移要等运行期基址（发现步骤，J3b）才能变成内核地址。 */
        val inputs = StepInputs()
        when (val opened = factory.open(AccessSessionRequest(backend, ByteArray(0)))) {
            is AccessResult.Failure -> {
                line("plan: " + backend + " session refused (" + opened.detail + ")")
                return PlanOutcome(backend, profileAsset, numbers, emptyList(), null, null)
            }

            is AccessResult.Success -> {
                val session = opened.value
                if (session !is JniAccessSession) {
                    return PlanOutcome(backend, profileAsset, numbers, emptyList(), null, null)
                }
                val engine = StepEngine(JniOpsInvoker(session)) { entry -> GhostLog.d(TAG, entry) }
                val results = engine.run(steps, inputs)
                for (result in results) {
                    line(
                        "plan: " + backend + " step=" + result.step.token + " outcome=" + result.outcome +
                            (result.reason?.let { " reason=" + it } ?: "") +
                            " calls=" + result.calls.joinToString(","),
                    )
                }
                /* 终态交接：跑完再问一次会话自己的相位，并带上清理状态。 */
                val phase = when (val snapshot = session.invoke(NativeProtocol.Op.Snapshot)) {
                    is AccessResult.Success ->
                        snapshot.value.u64(NativeProtocol.Key.Phase)?.toString()

                    is AccessResult.Failure -> null
                }
                val cleanup = session.cleanup.name
                session.close()
                val outcome = PlanOutcome(backend, profileAsset, numbers, results, phase, cleanup)
                line(outcome.summary())
                return outcome
            }
        }
    }

    private fun line(text: String): String {
        GhostLog.i(TAG, text)
        return text
    }
}
