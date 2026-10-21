package com.ghostlock.app.core.access

import com.ghostlock.app.core.AccessFailure
import com.ghostlock.app.core.AccessResult

/**
 * 访问能力父契约（Kotlin 侧，2026-10-18 架构裁决）。
 *
 * 职责边界：
 *  - Kotlin 是会话编排者：选择 backend、配置、初始化、步骤顺序、结果展示、最终 shutdown；
 *  - C++ 是原语资源所有者：一次 native 事务内的线程/waiter/映射/fd/socket/SA/sequence
 *    由 C++ 创建、停止、同步并释放；
 *  - 这里的能力对象只是**借用视图**，不拥有 native handle，也不各自关闭。
 *
 * 契约是开放扩展的：KernelMemoryAccess / PageCacheAccess 是当前有依据的两个子契约，
 * 不是所有未来 backend 必须二选一的全集。未来子接口由新原语的目标域决定。
 */
data class KernelAddress(val value: Long) {
    override fun toString(): String = "0x" + value.toULong().toString(16)
}

/** 传输约束：实际粒度与单次上限，来自 native 当前验证结果。 */
data class TransferConstraint(val granularity: Int, val maxBytes: Int)

/** 内核地址范围（闭区间），由 native 报告，不由 CVE 编号推断。 */
data class KernelAddressRange(val start: Long, val endInclusive: Long) {
    fun contains(address: Long): Boolean = address in start..endInclusive
}

/** 每次能力变化都带 generation；旧视图在 generation 变化后失效。 */
sealed interface CapabilityDescriptor {
    val generation: UInt
}

/**
 * 内核内存描述符：read / write 为 null 表示该方向尚未建立。
 * 64560 这类 backend 会分阶段把方向从 null 变为可用，而不是创建会话即宣告可读写。
 */
data class KernelMemoryDescriptor(
    val read: TransferConstraint?,
    val write: TransferConstraint?,
    val addressRanges: List<KernelAddressRange>,
    override val generation: UInt,
) : CapabilityDescriptor {
    val ready: Boolean get() = (read != null || write != null) && addressRanges.isNotEmpty()
}

data class PageCacheTarget(val id: String, val path: String, val writable: Boolean)

data class PageCacheDescriptor(
    val targets: List<PageCacheTarget>,
    val blockWidth: Int,
    val writable: Boolean,
    override val generation: UInt,
) : CapabilityDescriptor

enum class EffectState {
    /** native 回执确认该次写已生效且资源已恢复。 */
    CONFIRMED,

    /** 写可能已发生但效果/恢复不确定；Kotlin 不得当作普通失败重试。 */
    UNKNOWN,
}

data class WriteReceipt(
    val bytes: Int,
    val effect: EffectState,
    val generation: UInt,
)

/** 镜像写入回执：块写入之外还带回 sequence，供同一 SA 的后续窗口接续。 */
data class ImageWriteReceipt(
    val bytes: Int,
    val effect: EffectState,
    val generation: UInt,
    val sequence: Long = 0L,
)

/** 所有能力视图的公共面：只读描述符，不拥有生命周期。 */
interface AccessCapability {
    val descriptor: CapabilityDescriptor
}

interface KernelMemoryAccess : AccessCapability {
    override val descriptor: KernelMemoryDescriptor

    suspend fun read(address: KernelAddress, bytes: Int): AccessResult<ByteArray>
    suspend fun write(address: KernelAddress, bytes: ByteArray): AccessResult<WriteReceipt>
}

interface PageCacheAccess : AccessCapability {
    override val descriptor: PageCacheDescriptor

    suspend fun targets(): AccessResult<List<PageCacheTarget>>
    suspend fun read(target: PageCacheTarget, offset: Long, bytes: Int): AccessResult<ByteArray>
    suspend fun write(target: PageCacheTarget, offset: Long, bytes: ByteArray): AccessResult<WriteReceipt>

    /**
     * 镜像写入：按块宽切分后逐块 [write]。
     *
     * 这是业务数据面（模块/载荷镜像）的唯一入口；具体 backend 可以覆盖成一次 native
     * 镜像写入，但调用方只认这个契约。
     */
    suspend fun writeImage(
        target: PageCacheTarget,
        image: ByteArray,
        offset: Long,
        width: Int = 16,
    ): AccessResult<ImageWriteReceipt> {
        if (width <= 0 || image.isEmpty() || image.size % width != 0) {
            return AccessResult.Failure(AccessFailure.INVALID_REQUEST, "image width or size is invalid")
        }
        var written = 0
        var lastGeneration = descriptor.generation
        var at = 0
        while (at < image.size) {
            val block = image.copyOfRange(at, at + width)
            val receipt = write(target, offset + at, block)
            if (receipt is AccessResult.Failure) return receipt
            val ok = receipt as AccessResult.Success
            written += ok.value.bytes
            lastGeneration = ok.value.generation
            at += width
        }
        return AccessResult.Success(ImageWriteReceipt(written, EffectState.CONFIRMED, lastGeneration))
    }
}

/** 能力集合：没有的能力为 null，而不是抛异常。 */
interface AccessCapabilities {
    val kernelMemory: KernelMemoryAccess?
    val pageCache: PageCacheAccess?
}

data class SessionSnapshot(
    val phase: String,
    val capabilities: Map<String, CapabilityDescriptor>,
)

data class InitializationReceipt(val capabilities: List<CapabilityDescriptor>)

data class ShutdownReceipt(val clean: Boolean, val detail: String = "")

/**
 * Kotlin 会话编排面。
 *
 * 资源所有权仍在 C++/native 一侧；这里的实现只持有不透明 handle（LKM 场景下是 /dev/glk fd），
 * 并把同一 handle 上的调用串行化。子能力视图不提供 close。
 */
interface AccessSession : com.ghostlock.app.core.NativeSession {
    val capabilities: AccessCapabilities

    suspend fun snapshot(): AccessResult<SessionSnapshot>
    suspend fun initialize(): AccessResult<InitializationReceipt>
    suspend fun shutdown(): AccessResult<ShutdownReceipt>
}
