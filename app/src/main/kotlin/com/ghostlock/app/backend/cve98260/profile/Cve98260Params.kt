package com.ghostlock.app.backend.cve98260.profile

/**
 * 已解码的 C 侧参数（B2；service 进程使用）。
 *
 * service 不读 app assets：app 侧 `Cve98260ProfileConfig` 解析并校验后，把
 * `Cve98260Profile.toParamArg()` 经 AIDL `prepare(profileHex, payload)` 传入；
 * service 用本类按与 C `glk98260_params_parse` 相同规则解码（数量/字符集/fail-closed）。
 *
 * @property numeric 26 个数值参数位模式（顺序见 [Cve98260Profile.PARAM_NAMES]）。
 * @property managerPackage KernelSU manager 包名。
 * @property kmi late-load KMI。
 * @property lateLoadArgs late-load argv token（必须含 `--allow-shell`）。
 * @property ksudFallbacks ksud 回退路径。
 * @property raw 原始参数串（原样传给 payload `--params`，保证单权威）。
 */
internal data class Cve98260Params(
    val numeric: List<Long>,
    val managerPackage: String,
    val kmi: String,
    val lateLoadArgs: List<String>,
    val ksudFallbacks: List<String>,
    val raw: String,
) {
    /** @return 数值参数（越界即抛，调用方应先 [decode]）。 */
    internal fun number(index: Int): Long = numeric[index]

    companion object {
        private val CHARSET: Regex = Regex("[0-9a-fx,|A-Za-z0-9._/:\\-]+")

        /**
         * 解码 app 传入的参数串。
         *
         * @param arg `Cve98260Profile.toParamArg()` 的输出。
         * @return 解码结果。
         * @throws Cve98260ProfileException 具名 reason（`cve98260-params:*`）。
         */
        internal fun decode(arg: String): Cve98260Params {
            if (arg.isBlank() || arg.length > 2048 || !CHARSET.matches(arg)) {
                throw Cve98260ProfileException("cve98260-params:format")
            }
            val sep = arg.indexOf('|')
            if (sep <= 0) throw Cve98260ProfileException("cve98260-params:missing-strings")
            val parts = arg.substring(sep + 1).split('|')
            if (parts.size != 4) throw Cve98260ProfileException("cve98260-params:string-count")
            val numeric = arg.substring(0, sep).split(',').map { token ->
                if (!token.startsWith("0x") && !token.startsWith("0X")) {
                    throw Cve98260ProfileException("cve98260-params:number-not-hex")
                }
                val hex = token.substring(2)
                if (hex.isEmpty()) throw Cve98260ProfileException("cve98260-params:number-empty")
                runCatching { java.lang.Long.parseUnsignedLong(hex, 16) }.getOrElse {
                    throw Cve98260ProfileException("cve98260-params:number-invalid")
                }
            }
            if (numeric.size != Cve98260Profile.PARAM_COUNT) {
                throw Cve98260ProfileException("cve98260-params:number-count")
            }
            val manager = parts[0]
            val kmi = parts[1]
            val lateArgs = if (parts[2].isEmpty()) emptyList() else parts[2].split(':')
            val fallbacks = if (parts[3].isEmpty()) emptyList() else parts[3].split(':')
            if (manager.isEmpty() || kmi.isEmpty()) {
                throw Cve98260ProfileException("cve98260-params:strings")
            }
            if (lateArgs.size > 8 || lateArgs.any { it.isEmpty() || it.length > 128 }) {
                throw Cve98260ProfileException("cve98260-params:bad-token")
            }
            if (!lateArgs.contains("--allow-shell")) {
                throw Cve98260ProfileException("cve98260-params:missing-allow-shell")
            }
            if (fallbacks.size > 8 || fallbacks.any { it.isEmpty() || it.length > 128 }) {
                throw Cve98260ProfileException("cve98260-params:bad-token")
            }
            return Cve98260Params(numeric, manager, kmi, lateArgs, fallbacks, arg)
        }
    }
}
