package com.ghostlock.app.data.nativeaccess

import com.ghostlock.app.core.access.PageCacheOps
import com.ghostlock.app.core.access.PageCacheTarget

fun interface SessionInvoker {
    fun invoke(
        op: NativeProtocol.Op,
        values: Map<NativeProtocol.Key, ULong>,
        blobs: Map<NativeProtocol.Key, ByteArray>,
    ): com.ghostlock.app.core.AccessResult<NativeReply>
}

/**
 * 43284 页缓存能力的数据面：把 PageCacheOps 映射到既有四入口 JNI 会话。
 *
 * 控制面（hook/trigger/wait/LKM）不在这里，仍由 backend session 承担。
 */
class JniPageCacheOps(
    private val session: SessionInvoker,
    private val target: PageCacheTarget,
    private val role: ULong = ROLE_CARRIER,
) : PageCacheOps {
    private var token: ULong = 0uL
    private var width: Int = 0
    private var reason: String = ""

    override fun up(): Boolean {
        val snapshot = when (val answer = session.invoke(NativeProtocol.Op.Snapshot, emptyMap(), emptyMap())) {
            is com.ghostlock.app.core.AccessResult.Success -> answer.value
            is com.ghostlock.app.core.AccessResult.Failure -> { reason = answer.detail; return false }
        }
        if (!snapshot.ok) { reason = "snapshot-" + snapshot.status.token; return false }
        val flags = snapshot.u64(NativeProtocol.Key.Flags) ?: 0uL
        if ((flags and FLAG_PAGE_CACHE) == 0uL) { reason = "page-cache-flag-missing"; return false }
        width = ((flags shr WIDTH_SHIFT) and 0xFFuL).toInt()
        if (width != BLOCK_WIDTH) { reason = "width-" + width; return false }

        val resolved = when (val answer = session.invoke(
            NativeProtocol.Op.ResolveTarget,
            values = mapOf(NativeProtocol.Key.Mode to role),
            blobs = mapOf(NativeProtocol.Key.Path to target.path.toByteArray()),
        )) {
            is com.ghostlock.app.core.AccessResult.Success -> answer.value
            is com.ghostlock.app.core.AccessResult.Failure -> { reason = answer.detail; return false }
        }
        if (!resolved.ok) { reason = "resolve-" + resolved.status.token; return false }
        token = resolved.u64(NativeProtocol.Key.Token) ?: 0uL
        if (token == 0uL) reason = "token-empty"
        return token != 0uL
    }

    override fun targets(): List<PageCacheTarget> = if (token == 0uL) emptyList() else listOf(target)

    override fun readBlock(target: PageCacheTarget, offset: Long, out: ByteArray): Boolean {
        if (token == 0uL || target.id != this.target.id) return false
        val answer = when (val reply = session.invoke(
            NativeProtocol.Op.ReadBlock,
            values = mapOf(
                NativeProtocol.Key.Token to token,
                NativeProtocol.Key.Offset to offset.toULong(),
                NativeProtocol.Key.Length to out.size.toULong(),
            ),
            blobs = emptyMap(),
        )) {
            is com.ghostlock.app.core.AccessResult.Success -> reply.value
            is com.ghostlock.app.core.AccessResult.Failure -> return false
        }
        if (!answer.ok) return false
        val bytes = answer.blob(NativeProtocol.Key.Bytes) ?: return false
        if (bytes.size != out.size) return false
        bytes.copyInto(out)
        return true
    }

    override fun writeBlock(target: PageCacheTarget, offset: Long, block: ByteArray): Boolean {
        if (token == 0uL || target.id != this.target.id) return false
        val answer = when (val reply = session.invoke(
            NativeProtocol.Op.WriteBlock,
            values = mapOf(
                NativeProtocol.Key.Token to token,
                NativeProtocol.Key.Offset to offset.toULong(),
                NativeProtocol.Key.Length to block.size.toULong(),
            ),
            blobs = mapOf(NativeProtocol.Key.Bytes to block.copyOf()),
        )) {
            is com.ghostlock.app.core.AccessResult.Success -> reply.value
            is com.ghostlock.app.core.AccessResult.Failure -> return false
        }
        return answer.ok
    }

    /** 生命周期由 capability session 的 shutdown 统一收口；这里不单独关闭。 */
    override fun tearDown(): Boolean = true

    override fun lastReason(): String = reason.ifBlank { "page-cache landing is not up" }

    companion object {
        private val FLAG_PAGE_CACHE = 1uL shl 2
        private const val WIDTH_SHIFT = 8
        private const val BLOCK_WIDTH = 16
        private const val ROLE_CARRIER = 2uL
    }
}
