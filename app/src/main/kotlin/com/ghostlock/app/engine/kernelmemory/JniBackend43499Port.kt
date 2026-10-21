package com.ghostlock.app.engine.kernelmemory

import com.ghostlock.app.core.generated.CredentialLayout43499
import com.ghostlock.app.core.generated.Discover43499
import com.ghostlock.app.core.generated.FinishVictim43499
import com.ghostlock.app.core.generated.Init43499
import com.ghostlock.app.core.generated.KernelLayout43499
import com.ghostlock.app.core.generated.Prepare43499
import com.ghostlock.app.core.generated.TargetOffsets43499
import com.ghostlock.app.jni.cve43499.Native43499

/**
 * JNI 函数面的最小 seam：生产走 [AndroidNative43499Api]，测试注入 fake。
 *
 * 单独一层是为了让端口/解码/能力门可以在 JVM 上跑，不依赖 UnsatisfiedLinkError。
 */
internal interface Native43499Api {
    fun ensureLoaded(): Boolean
    fun create(): Long
    fun initialize(
        handle: Long,
        init: Init43499,
        layout: KernelLayout43499,
        credential: CredentialLayout43499,
        targets: TargetOffsets43499,
        refusalOut: Array<String>,
    ): Int
    fun snapshot(handle: Long, out: LongArray, refusalOut: Array<String>): Int
    fun prepare(handle: Long, request: Prepare43499, out: LongArray, refusalOut: Array<String>): Int
    fun discover(handle: Long, request: Discover43499, out: LongArray, refusalOut: Array<String>): Int
    fun spawnVictim(handle: Long, out: LongArray, refusalOut: Array<String>): Int
    fun finishVictim(handle: Long, request: FinishVictim43499, out: LongArray, refusalOut: Array<String>): Int
    fun victimProbe(handle: Long, kind: Int, out: LongArray, refusalOut: Array<String>): Int
    fun shutdown(handle: Long, refusalOut: Array<String>): Int
    fun destroy(handle: Long, refusalOut: Array<String>): Int
    fun snapshotFields(): Array<String>
}

/** Android 上的唯一实现：逐函数委托给既有 [Native43499]。 */
internal object AndroidNative43499Api : Native43499Api {
    override fun ensureLoaded(): Boolean = Native43499.ensureLoaded()
    override fun create(): Long = Native43499.create()
    override fun initialize(
        handle: Long,
        init: Init43499,
        layout: KernelLayout43499,
        credential: CredentialLayout43499,
        targets: TargetOffsets43499,
        refusalOut: Array<String>,
    ): Int = Native43499.initialize(handle, init, layout, credential, targets, refusalOut)
    override fun snapshot(handle: Long, out: LongArray, refusalOut: Array<String>): Int =
        Native43499.snapshot(handle, out, refusalOut)
    override fun prepare(handle: Long, request: Prepare43499, out: LongArray, refusalOut: Array<String>): Int =
        Native43499.prepare(handle, request, out, refusalOut)
    override fun discover(handle: Long, request: Discover43499, out: LongArray, refusalOut: Array<String>): Int =
        Native43499.discover(handle, request, out, refusalOut)
    override fun spawnVictim(handle: Long, out: LongArray, refusalOut: Array<String>): Int =
        Native43499.spawnVictim(handle, out, refusalOut)
    override fun finishVictim(
        handle: Long,
        request: FinishVictim43499,
        out: LongArray,
        refusalOut: Array<String>,
    ): Int = Native43499.finishVictim(handle, request, out, refusalOut)
    override fun victimProbe(handle: Long, kind: Int, out: LongArray, refusalOut: Array<String>): Int =
        Native43499.victimProbe(handle, kind, out, refusalOut)
    override fun shutdown(handle: Long, refusalOut: Array<String>): Int = Native43499.shutdown(handle, refusalOut)
    override fun destroy(handle: Long, refusalOut: Array<String>): Int = Native43499.destroy(handle, refusalOut)
    override fun snapshotFields(): Array<String> = Native43499.snapshotFields()
}

/**
 * 43499 的 JNI 端口（K1）。
 *
 * 规则：
 *  - 只认调用码 + 具名拒绝；未知码折成 failed，不猜成功；
 *  - 三个能力门保守：generations 由 snapshot 名字表判定；form/target 在 K1 没有探针，恒 false；
 *  - 能力门缺失时在触 native 前拒绝；
 *  - close 幂等，shutdown 与 destroy 都执行，任一失败都具名。
 *
 * 写路径：K1 的 native 面不能把 form/target/value 传到设备写，所以 [writeStep] 只回答
 * unsupported:form-not-propagated / unsupported:target-value-not-published；真正的每步事务
 * 在 native 计划 N2 落地。K1 不调用 writeWord/write43499 任何一条。
 */
class JniBackend43499Port internal constructor(
    private val native: Native43499Api = AndroidNative43499Api,
) : Backend43499Port {
    private var handle: Long = 0L
    private var closed = false

    private var fieldNames: Array<String>? = null

    /**
     * 读一次 snapshot 名字表；失败时返回 null 并在下次重试。
     *
     * 不在构造/属性初始化时读：那时库可能还没 load，缓存 null 会把能力门永久锁死。
     */
    private fun names(): Array<String>? {
        fieldNames?.let { return it }
        if (!native.ensureLoaded()) return null
        val loaded = runCatching { native.snapshotFields() }
            .getOrNull()
            ?.takeIf { it.isNotEmpty() }
        fieldNames = loaded
        return loaded
    }

    override val capabilities: NativeCapabilities
        get() {
            val fields = names().orEmpty()
            return NativeCapabilities(
                generations = Facts43499.GENERATION_FIELDS.all { fields.contains(it) },
                formPropagation = false,
                perStepTargetValue = false,
            )
        }

    override fun create(): PortResult<Unit> {
        if (closed) return refused(PortCode.CLOSED, "session-closed")
        if (!native.ensureLoaded()) return refused(PortCode.UNSUPPORTED, "library-missing")
        val made = native.create()
        if (made <= 0L) return refused(PortCode.FAILED, "create-refused")
        handle = made
        return PortResult.Ok(Unit)
    }

    override fun initialize(session: KernelSession): PortResult<Unit> {
        if (handle <= 0L) return refused(PortCode.CLOSED, "session-closed")
        val refusal = refusalSlot()
        val code = native.initialize(
            handle, session.init, session.layout, session.credential, session.targets, refusal,
        )
        return answer(code, refusal)
    }

    override fun prepare(request: Prepare43499): PortResult<Facts43499> =
        readSnapshot { out, refusal -> native.prepare(handle, request, out, refusal) }

    override fun discover(request: Discover43499): PortResult<Facts43499> {
        if (!capabilities.generations) return refused(PortCode.UNSUPPORTED, "generation-not-published")
        return readSnapshot { out, refusal -> native.discover(handle, request, out, refusal) }
    }

    override fun spawnVictim(): PortResult<Facts43499> =
        readSnapshot { out, refusal -> native.spawnVictim(handle, out, refusal) }

    override fun victimProbe(kind: Int): PortResult<Long> {
        if (handle <= 0L) return refused(PortCode.CLOSED, "session-closed")
        val out = LongArray(1)
        val refusal = refusalSlot()
        val code = native.victimProbe(handle, kind, out, refusal)
        return if (code == 0) PortResult.Ok(out[0]) else refused(codeOf(code), detail(code, refusal))
    }

    override fun finishVictim(action: Int, victimGeneration: Long): PortResult<Facts43499> {
        if (!capabilities.generations) return refused(PortCode.UNSUPPORTED, "generation-not-published")
        val request = FinishVictim43499(action, victimGeneration)
        return readSnapshot { out, refusal -> native.finishVictim(handle, request, out, refusal) }
    }

    override fun writeStep(
        step: KernelStep,
        address: Long,
        value: Long,
        form: Int,
        offset: Long,
    ): PortResult<Unit> {
        /* K1 的 native 面没有 form/target/value 的传输通道：拒绝而不是用错误形状写。 */
        if (!capabilities.formPropagation) return refused(PortCode.UNSUPPORTED, "form-not-propagated")
        if (!capabilities.perStepTargetValue) return refused(PortCode.UNSUPPORTED, "target-value-not-published")
        if (!capabilities.generations) return refused(PortCode.UNSUPPORTED, "generation-not-published")
        return refused(PortCode.UNSUPPORTED, "write-contract-not-implemented")
    }

    override fun close(): PortResult<Unit> {
        if (closed) return PortResult.Ok(Unit)
        closed = true
        val held = handle
        handle = 0L
        if (held <= 0L) return PortResult.Ok(Unit)
        val shutdownRefusal = refusalSlot()
        val shutdown = runCatching { native.shutdown(held, shutdownRefusal) }.getOrElse { -4 }
        val destroyRefusal = refusalSlot()
        val destroy = runCatching { native.destroy(held, destroyRefusal) }.getOrElse { -4 }
        if (shutdown == 0 && destroy == 0) return PortResult.Ok(Unit)
        val why = "shutdown=" + detail(shutdown, shutdownRefusal) +
            " destroy=" + detail(destroy, destroyRefusal)
        return refused(PortCode.FAILED, why)
    }

    private inline fun readSnapshot(
        call: (LongArray, Array<String>) -> Int,
    ): PortResult<Facts43499> {
        if (handle <= 0L) return refused(PortCode.CLOSED, "session-closed")
        val names = names() ?: return refused(PortCode.FAILED, "snapshot-fields-unavailable")
        val out = LongArray(names.size)
        val refusal = refusalSlot()
        val code = call(out, refusal)
        if (code != 0) return refused(codeOf(code), detail(code, refusal))
        val decoded = runCatching { Facts43499.decode(names, out) }.getOrElse { problem ->
            return refused(PortCode.FAILED, "snapshot-decode:" + (problem.message ?: "invalid"))
        }
        if (decoded.width != 8L) {
            return refused(PortCode.BAD_PARAMETERS, "snapshot-width:" + decoded.width)
        }
        return PortResult.Ok(decoded)
    }

    private fun answer(code: Int, refusal: Array<String>): PortResult<Unit> =
        if (code == 0) PortResult.Ok(Unit) else refused(codeOf(code), detail(code, refusal))

    private fun refused(code: PortCode, detail: String): PortResult<Nothing> =
        PortResult.Refused(PortRefusal(code, detail))

    private fun refusalSlot(): Array<String> = arrayOf("")

    private fun detail(code: Int, refusal: Array<String>): String =
        refusal.getOrNull(0)?.takeIf { it.isNotBlank() } ?: ("call-code:" + code)

    private fun codeOf(code: Int): PortCode = when (code) {
        -1 -> PortCode.NOT_READY
        -2 -> PortCode.BAD_PARAMETERS
        -3 -> PortCode.UNSUPPORTED
        -4 -> PortCode.FAILED
        -5 -> PortCode.STALE_HANDLE
        -6 -> PortCode.BUSY
        -7 -> PortCode.STALE_TARGET
        -8 -> PortCode.IN_FLIGHT
        -9 -> PortCode.CLOSED
        else -> PortCode.FAILED
    }
}
