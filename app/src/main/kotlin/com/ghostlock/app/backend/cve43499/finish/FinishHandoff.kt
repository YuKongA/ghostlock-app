package com.ghostlock.app.backend.cve43499.finish

import java.security.MessageDigest

/**
 * UI 生成的收尾脚本的不可变交接对象：脚本路径 + 内容 + SHA-256。
 *
 * 契约（`docs/plan/payload-finish-chain-plan.md` §1/§2）：
 * - 脚本由 UI 侧用既有 `FinishState.toRootScript` 渲染、由 `FinishScriptStore` 落盘；
 * - 后端只持有**只读证据**：不生成、不选择、不解释脚本，也不 import `ui` 包；
 * - `run.sh` 是临时物，脚本内容与路径在 handoff 里冻结，验收时用 SHA-256 对拍
 *   同一个脚本对象（不是对拍 terminal 输出）。
 *
 * @param scriptPath 脚本对象的绝对路径（`files/finish/run.sh`）。
 * @param scriptBytes 脚本内容副本；[copyScriptBytes] 每次返回新的副本。
 * @param scriptSha256Hex 脚本内容的 SHA-256 小写十六进制（64 字符）。
 * @note 阶段：G4L-6 finish 交接；不可变、无线程安全状态。
 */
internal class FinishHandoff private constructor(
    val scriptPath: String,
    scriptBytes: ByteArray,
    val scriptSha256Hex: String,
) {
    private val scriptBytes: ByteArray = scriptBytes.copyOf()

    /** @return 脚本字节数（证据用；不是业务结论）。 */
    val scriptSizeBytes: Int get() = scriptBytes.size

    /**
     * @return 脚本内容的一份新副本；调用方不得缓存或写回脚本对象。
     * @note 返回副本，handoff 本身的不可变性不依赖调用方纪律。
     */
    fun copyScriptBytes(): ByteArray = scriptBytes.copyOf()

    companion object {
        /** SHA-256 十六进制文本长度。 */
        const val SHA256_HEX_LENGTH: Int = 64

        /** 合法 SHA-256 十六进制字符集（小写）。 */
        private const val HEX_CHARS = "0123456789abcdef"

        /**
         * 计算字节数组的 SHA-256 小写十六进制。
         *
         * @param bytes 任意字节。
         * @return 64 字符小写十六进制文本。
         */
        internal fun sha256Hex(bytes: ByteArray): String {
            val digest = MessageDigest.getInstance("SHA-256").digest(bytes)
            val builder = StringBuilder(digest.size * 2)
            digest.forEach { byte ->
                val value = byte.toInt() and 0xFF
                builder.append(HEX_CHARS[value ushr 4])
                builder.append(HEX_CHARS[value and 0x0F])
            }
            return builder.toString()
        }

        /**
         * 由脚本路径与内容创建 handoff（SHA-256 在内部计算）。
         *
         * @param scriptPath 非空脚本绝对路径。
         * @param scriptBytes 非空脚本内容。
         * @return handoff；参数不合法返回 null（调用方按 `finish-handoff-invalid` 拒绝）。
         */
        internal fun create(scriptPath: String, scriptBytes: ByteArray): FinishHandoff? {
            if (scriptPath.isBlank() || scriptBytes.isEmpty()) return null
            val sha = sha256Hex(scriptBytes)
            return create(scriptPath, scriptBytes, sha)
        }

        /**
         * 由脚本路径、内容与调用方给出的 SHA-256 创建 handoff，并校验一致性。
         *
         * @param scriptPath 非空脚本绝对路径。
         * @param scriptBytes 非空脚本内容。
         * @param scriptSha256Hex 声称的 SHA-256；必须与内容一致。
         * @return handoff；内容/长度/摘要不合法时返回 null。
         */
        internal fun create(
            scriptPath: String,
            scriptBytes: ByteArray,
            scriptSha256Hex: String,
        ): FinishHandoff? {
            if (scriptPath.isBlank() || scriptBytes.isEmpty()) return null
            if (scriptSha256Hex.length != SHA256_HEX_LENGTH) return null
            if (!scriptSha256Hex.all { it in HEX_CHARS }) return null
            if (sha256Hex(scriptBytes) != scriptSha256Hex) return null
            return FinishHandoff(scriptPath, scriptBytes, scriptSha256Hex)
        }
    }
}
