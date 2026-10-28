package com.ghostlock.app.ui.backend

import android.content.Context
import com.ghostlock.app.backend.BackendId
import com.ghostlock.app.backend.cve43499.flow.Flow43499Impl
import com.ghostlock.app.backend.cve98260.flow.Flow98260Impl
import com.ghostlock.app.core.flow.FlowProgress
import com.ghostlock.app.ui.screen.module.backends.Cve43499RequestBuild
import com.ghostlock.app.ui.screen.module.backends.Cve43499RequestFactory
import com.ghostlock.app.ui.screen.module.backends.Cve98260RequestBuild
import com.ghostlock.app.ui.screen.module.backends.Cve98260RequestFactory
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * backend 的 run 入口（flow runner，B3/R1a）。
 *
 * **单一权威**：`BackendSpec.runner != null` ⇔ 该 backend 可运行（`runCapability` 由它派生）；
 * dispatcher 只调用 spec 声明的 runner，不再按 `BackendId` 写 `when` 分支（杜绝第二注册点）。
 *
 * 实现约束：每次 [run] **新建 Flow 实例**（Flow 是一次性对象）；异常在 runner 内映射为具名
 * `Refused`（前缀 `cve<id>-run-execution:exception:`），取消原样抛出；不伪造成功。
 */
interface BackendFlowRunner {
    /** 与 `BackendSpec.key` 一致（动态 backend 用唯一 key；dispatcher 直接用 spec.runner）。 */
    val key: String

    /**
     * 执行一次已获门禁允许的 backend run。
     *
     * @param context 应用上下文（输入组装/端口注入）。
     * @param onLine 运行期事实行回调（终端展示；不参与控制流）。
     * @return 执行结果（Finished/Refused）；异常已映射为 Refused，不抛给 UI。
     */
    suspend fun run(context: Context, onLine: (String) -> Unit = {}): BackendRunUiState
}

/**
 * CVE-2026-43499 的 runner（把原 dispatcher 分支原样搬入；行为不变）。
 *
 * @note 输入组装失败=Refused（具名 reasons）；Flow 异常=Refused 前缀 `cve43499-run-execution:exception:`。
 */
internal object Cve43499FlowRunner : BackendFlowRunner {
    /** Flow 抛异常时的具名 reason 前缀（执行期失败，不是 allow/deny 决策）。 */
    private const val REASON_EXCEPTION_PREFIX: String = "cve43499-run-execution:exception:"

    override val key: String = "CVE-2026-43499"

    override suspend fun run(context: Context, onLine: (String) -> Unit): BackendRunUiState {
        val build = withContext(Dispatchers.IO) {
            Cve43499RequestFactory.build(
                context.applicationContext,
                progress = FlowProgress { line -> onLine(line) },
            )
        }
        return when (build) {
            is Cve43499RequestBuild.Refused -> BackendRunUiState.Refused(build.reasons)
            is Cve43499RequestBuild.Built -> try {
                /* Flow 由攻击线程（AttackThread）执行；不再切回 IO 线程。 */
                val result = Flow43499Impl().run(build.request)
                BackendRunUiState.Finished(result)
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (problem: Throwable) {
                BackendRunUiState.Refused(
                    listOf(REASON_EXCEPTION_PREFIX + problem::class.java.simpleName),
                )
            }
        }
    }
}

/**
 * CVE-2026-98260 的 runner（原 dispatcher 分支搬入；进度行回传终端）。
 *
 * @note 输入组装失败=Refused；Flow 异常=Refused 前缀 `cve98260-run-execution:exception:`。
 */
internal object Cve98260FlowRunner : BackendFlowRunner {
    /** Flow 抛异常时的具名 reason 前缀（执行期失败，不是 allow/deny 决策）。 */
    private const val REASON_EXCEPTION_PREFIX: String = "cve98260-run-execution:exception:"

    override val key: String = "CVE-2026-98260"

    override suspend fun run(context: Context, onLine: (String) -> Unit): BackendRunUiState {
        val progress = FlowProgress { line -> onLine(line) }
        val build = withContext(Dispatchers.IO) {
            Cve98260RequestFactory.build(context.applicationContext, progress)
        }
        return when (build) {
            is Cve98260RequestBuild.Refused -> BackendRunUiState.Refused(build.reasons)
            is Cve98260RequestBuild.Built -> try {
                val result = Flow98260Impl().run(build.request)
                BackendRunUiState.Finished(result)
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (problem: Throwable) {
                BackendRunUiState.Refused(
                    listOf(REASON_EXCEPTION_PREFIX + problem::class.java.simpleName),
                )
            }
        }
    }
}
