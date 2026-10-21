package com.ghostlock.app.engine.kernelmemory

import com.ghostlock.app.core.generated.CredentialLayout43499
import com.ghostlock.app.core.generated.Discover43499
import com.ghostlock.app.core.generated.Init43499
import com.ghostlock.app.core.generated.KernelLayout43499
import com.ghostlock.app.core.generated.Prepare43499
import com.ghostlock.app.core.generated.TargetOffsets43499
import com.ghostlock.app.core.steps.StepId

/**
 * 一次 43499 运行的全部输入，由调用方注入。
 *
 * 这里没有 UI、没有 profile 解析、没有默认值：缺什么由引擎具名拒绝，不猜。
 *
 * @property session 四组 init DTO（native 的 SessionParams43499）。
 * @property prepare 准备请求。
 * @property discover 发现请求工厂：prepareGeneration 必须来自上一次 prepare 的 facts。
 * @property steps W1/W2/W3 的运行期目标（route/offset/address/target/value）。
 * @property expectedUid 成功判据；默认 0。
 * @property finishAction 0=retire，1=park；2=handoff 在 K1 具名拒绝（native 计划 N3 再议）。
 * @property attempts PipelineEngine 的重试次数（不重试=1）。
 * @property settleMs PipelineEngine 两次尝试之间的等待。
 */
data class KernelMemoryRun(
    val session: KernelSession,
    val prepare: Prepare43499,
    val discover: (prepareGeneration: Long) -> Discover43499,
    val steps: Map<StepId, KernelStep>,
    val expectedUid: Long = 0L,
    val finishAction: Int = 0,
    val attempts: Int = 1,
    val settleMs: Long = 0L,
)

/** 四组 init DTO 一起给；native 缺字段按名拒绝。 */
data class KernelSession(
    val init: Init43499,
    val layout: KernelLayout43499,
    val credential: CredentialLayout43499,
    val targets: TargetOffsets43499,
)

/**
 * 一步的运行期目标。
 *
 * @property step 步骤身份（W1/W2/W3）。
 * @property route route 枚举值（由调用方从档案/输入给，引擎不猜）。
 * @property offset route 内偏移。
 * @property address 写入地址（也是 StepPlans 里的 address）。
 * @property target route 守卫对的目标；K1 要求它**等于 [address]**（native D2 不另存 target）。
 * @property value route 守卫对的值（W2 为凭据值，W1/W3 为 0）。
 *
 * 每步一次访问事务：target/value 固定在一份 route 配置里，所以不能拿一个 access 连写三步。
 */
data class KernelStep(
    val step: StepId,
    val route: Int,
    val offset: Long,
    val address: Long,
    val target: Long,
    val value: Long,
)
