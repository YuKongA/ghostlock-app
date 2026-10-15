package com.ghostlock.app.tempui

import com.ghostlock.app.core.AccessResult
import com.ghostlock.app.core.AccessSessionRequest
import com.ghostlock.app.core.steps.PlanCatalog
import com.ghostlock.app.core.steps.StepEngine
import com.ghostlock.app.core.steps.StepInputs
import com.ghostlock.app.core.steps.StepOutcome
import com.ghostlock.app.data.nativeaccess.JniAccessSession
import com.ghostlock.app.data.nativeaccess.JniAccessSessionFactory
import com.ghostlock.app.data.nativeaccess.JniOpsInvoker
import com.ghostlock.app.log.GhostLog

/**
 * 在设备上跑一遍声明的计划：装配 [StepEngine]，把每一步的结局写进日志。
 *
 * 它今天能得到的结论是**诚实的拒绝链**：43499 的三个写步骤会停在 not-ready:prepare（设备 seam 未接），
 * 43284 的 patch1 会停在 op-not-wired:resolve_target（门未接）。这一批要验的不是"能不能打"，而是
 * "顺序对不对、缺前置会不会空跑、拒绝会不会停下、结果有没有留痕"。
 *
 * 输入里给的都是**占位**值（地址/路径），真值由 profile 装载层在 J8 提供；给占位值的意义是让每一步
 * 走到它该走的那次调用，而不是在引擎的输入检查处就短路。
 */
object PlanRunner {
    /** 计划日志的 logcat 标签。 */
    const val TAG = "GhostlockPlan"

    /** 占位输入：见类注释；真值来自 profile（J8）。 */
    private fun placeholderInputs(backend: String): StepInputs = if (backend == "cve_2026_43499") {
        StepInputs(
            selinuxAddress = 0xffffff8042a00000uL,
            credentialAddress = 0xffffff8042b00000uL,
            credentialValue = 0xffffff8042c00000uL,
            seccompAddress = 0xffffff8042d00000uL,
            writeOffset = 0uL,
        )
    } else {
        StepInputs(
            patch1Path = "/system/lib64/libbinderdebug.so".toByteArray(),
            carrierPath = "/vendor/lib64/libghostlock-carrier.so".toByteArray(),
            hookPath = "/system/lib64/libc++.so".toByteArray(),
            carrierBase = 0uL,
            triggerDelayMs = 150uL,
            waitBudgetMs = 800uL,
            waitPollMs = 10uL,
            moduleName = "ghostlock".toByteArray(),
        )
    }

    /** 跑两个 backend 的声明计划。@return 每一行（屏幕与 logcat 内容相同）。 */
    fun run(factory: JniAccessSessionFactory = JniAccessSessionFactory()): List<String> {
        val out = mutableListOf<String>()
        for (backend in listOf("cve_2026_43499", "cve_2026_43284")) {
            when (val opened = factory.open(AccessSessionRequest(backend, ByteArray(0)))) {
                is AccessResult.Failure -> out += line("plan: " + backend + " open failed: " + opened.detail)

                is AccessResult.Success -> {
                    val session = opened.value
                    if (session !is JniAccessSession) {
                        out += line("plan: " + backend + " unexpected session implementation")
                        continue
                    }
                    val engine = StepEngine(JniOpsInvoker(session)) { entry -> GhostLog.d(TAG, entry) }
                    val results = engine.run(PlanCatalog.declaredSteps(backend), placeholderInputs(backend))
                    for (result in results) {
                        out += line(
                            "plan: " + backend + " step=" + result.step.token + " outcome=" + result.outcome +
                                (result.reason?.let { " reason=" + it } ?: "") +
                                " calls=" + result.calls.joinToString(","),
                        )
                    }
                    session.close()
                }
            }
        }
        return out
    }

    private fun line(text: String): String {
        GhostLog.i(TAG, text)
        return text
    }
}
