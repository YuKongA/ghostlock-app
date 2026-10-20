package com.ghostlock.app.engine.pagecache

import com.ghostlock.app.engine.pagecache.stage.StageLog
import com.ghostlock.app.engine.pagecache.stage.silentStageLog

/**
 * 在内存中的 `.ko` 镜像里写入脚本路径。
 *
 * 布局由 `tools/lkm/ghostlock/ghostlock.c` 的 `struct glk_knob` 固定：
 *
 * ```text
 * 0   magic[8]          "GLKKNOB1"
 * 8   size   u32        sizeof(struct glk_knob) == 284
 * 12  permissive  u32
 * 16  defex       u32
 * 20  restore_enforce u32
 * 24  path_len    u32
 * 28  script_path[256]
 * ```
 *
 * 只改数据段；ELF 头、`.modinfo`、符号表保持原样。找不到唯一 magic、尺寸不符或
 * 路径非法时抛 [LkmPatchException]，调用方按具名失败处理，不触发。
 */
object LkmImagePatch {
    /** 脚本路径槽容量（含结尾 NUL）。 */
    const val PATH_CAPACITY: Int = 256

    /** knob 结构总长。 */
    const val KNOB_SIZE: Int = 284

    private val MAGIC = "GLKKNOB1".toByteArray(Charsets.US_ASCII)

    /** 补丁失败原因；`reason` 直接进日志。 */
    class LkmPatchException(val reason: String) : Exception(reason)

    /**
     * 把 [scriptPath] 写进镜像副本。
     *
     * @param image 原始 `.ko` 字节（不会被修改）。
     * @param scriptPath 绝对路径，长度 ≤255（含 NUL 后 ≤256）。
     * @return 新的镜像字节。
     * @throws LkmPatchException magic 缺失/重复、尺寸不符或路径非法。
     */
    fun apply(image: ByteArray, scriptPath: String, log: StageLog = silentStageLog): ByteArray {
        log.verbose("LkmImagePatch.apply", "image" to image.size, "script" to scriptPath)
        if (image.size < KNOB_SIZE) {
            log.debug("LkmImagePatch.apply", "image-too-small", "bytes" to image.size)
            throw LkmPatchException("knob-image-too-small")
        }
        val at = locate(image, log)
        if (readU32(image, at + 8) != KNOB_SIZE) {
            throw LkmPatchException("knob-size-mismatch")
        }
        val path = scriptPath.toByteArray(Charsets.UTF_8)
        if (!scriptPath.startsWith("/") || path.size >= PATH_CAPACITY || scriptPath.indexOf('\u0000') >= 0) {
            log.debug("LkmImagePatch.apply", "path-invalid", "bytes" to path.size)
            throw LkmPatchException("knob-path-invalid")
        }
        val out = image.copyOf()
        /* permissive/defex/restore_enforce 保留 KO 默认值；B8 只写路径。 */
        writeU32(out, at + 24, path.size)
        for (index in 0 until PATH_CAPACITY) {
            out[at + 28 + index] = 0
        }
        path.copyInto(out, at + 28)
        log.debug("LkmImagePatch.apply", "ok", "at" to at, "pathBytes" to path.size)
        return out
    }

    /** 定位唯一 magic；0 份或多份都抛具名异常。 */
    private fun locate(image: ByteArray, log: StageLog = silentStageLog): Int {
        var found = -1
        var index = 0
        while (index <= image.size - MAGIC.size) {
            var hit = true
            for (offset in MAGIC.indices) {
                if (image[index + offset] != MAGIC[offset]) {
                    hit = false
                    break
                }
            }
            if (hit) {
                if (found >= 0) {
                    log.debug("LkmImagePatch.locate", "duplicate", "at" to index)
                    throw LkmPatchException("knob-magic-duplicate")
                }
                found = index
                index += MAGIC.size
            } else {
                index += 1
            }
        }
        if (found < 0) {
            log.debug("LkmImagePatch.locate", "missing")
            throw LkmPatchException("knob-magic-missing")
        }
        return found
    }

    private fun readU32(bytes: ByteArray, at: Int): Int =
        (bytes[at].toInt() and 0xFF) or
            ((bytes[at + 1].toInt() and 0xFF) shl 8) or
            ((bytes[at + 2].toInt() and 0xFF) shl 16) or
            ((bytes[at + 3].toInt() and 0xFF) shl 24)

    private fun writeU32(bytes: ByteArray, at: Int, value: Int) {
        bytes[at] = (value and 0xFF).toByte()
        bytes[at + 1] = ((value ushr 8) and 0xFF).toByte()
        bytes[at + 2] = ((value ushr 16) and 0xFF).toByte()
        bytes[at + 3] = ((value ushr 24) and 0xFF).toByte()
    }
}
