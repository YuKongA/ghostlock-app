package com.ghostlock.app.engine.pagecache

import com.ghostlock.app.core.AccessResult
import com.ghostlock.app.core.access.EffectState
import com.ghostlock.app.core.access.PageCacheAccess
import com.ghostlock.app.core.access.PageCacheTarget

/** 一次 Kotlin 块写的账。 */
data class KotlinWriteSummary(
    val written: Int,
    val verified: Int,
    val failedAt: Long? = null,
    val reason: String = "",
) {
    val ok: Boolean get() = failedAt == null && written > 0 && verified == written
}

/**
 * 逐块写页缓存；写入只走 [PageCacheAccess.write]。
 *
 * 校验优先直接读目标文件（目标可读时），否则退回 [PageCacheAccess.read]；
 * 两种都读不到就只记 written，不伪造 verified。
 */
object KotlinPageCacheWriter {
    suspend fun writeImage(
        access: PageCacheAccess,
        target: PageCacheTarget,
        image: ByteArray,
        offset: Long,
        directVerify: ((Long, ByteArray) -> Boolean)? = null,
    ): KotlinWriteSummary {
        if (image.isEmpty() || image.size % 16 != 0) {
            return KotlinWriteSummary(0, 0, offset, "image-not-block-aligned")
        }
        var written = 0
        var verified = 0
        var at = 0
        while (at < image.size) {
            val block = image.copyOfRange(at, at + 16)
            val write = access.write(target, offset + at, block)
            if (write is AccessResult.Failure) {
                return KotlinWriteSummary(written, verified, offset + at, write.detail)
            }
            if (write is AccessResult.Success && write.value.effect != EffectState.CONFIRMED) {
                return KotlinWriteSummary(written, verified, offset + at, "effect-" + write.value.effect)
            }
            written += 1
            val direct = directVerify?.invoke(offset + at, block)
            if (direct == true) {
                verified += 1
            } else if (direct == null) {
                val read = access.read(target, offset + at, 16)
                if (read is AccessResult.Success && read.value.contentEquals(block)) {
                    verified += 1
                }
            }
            at += 16
        }
        return KotlinWriteSummary(written, verified)
    }
}
