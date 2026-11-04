package com.ghostlock.app.backend.backends.cve43499glued

import android.content.Context
import com.ghostlock.app.core.flow.FlowHostEnv
import com.ghostlock.app.backend.declarations.BackendFlowParams

/**
 * 43499-Glued 的 Flow 参数构造/解码（B4a 附录 §2）。
 *
 * UI 进程（有 Context）把 profile 解析成三个扁平参数：HOCON 源文本、GLK1 字节、release。
 * host 进程（无 UI 全局）只从 `env.params` 解码；不得读 [BackendCve43499Glued.profileReference]。
 *
 * @note 纯数据；profile 解析走既有 [Glued43499ProfileCodec]（单一权威）。
 * @note 阶段：B4a。
 */
object Glued43499ExecParams {
    /** profile HOCON 源文本。 */
    const val KEY_PROFILE_CONF: String = "profile.conf"

    /** 当前 GLK1 字节。 */
    const val KEY_PROFILE_BIN: String = "profile.bin"

    /** profile release 标识（证据用）。 */
    const val KEY_PROFILE_RELEASE: String = "profile.release"

    /** host 侧解码结果。 */
    data class ParsedProfile(
        val conf: String,
        val bin: ByteArray,
        val release: String,
    )

    /**
     * UI 进程构造参数（生产入口）。
     *
     * @param context app context（profile assets/SAF URI）。
     * @return Ready（三参数）或 Refused（`cve43499:profile-invalid:*`）。
     */
    fun build(context: Context): BackendFlowParams =
        build(BackendCve43499Glued.profileReference) { reference ->
            Glued43499ProfileCodec.resolve(context, reference)
        }

    /**
     * 纯函数：把已解析 profile 映射为 Flow 参数（build 委托本函数）。
     *
     * @param profile 已解析 profile（sourceText/binary/release）。
     * @return Ready（三参数；字节内容逐字节保留）。
     */
    internal fun fromResolved(profile: Resolved43499Profile): BackendFlowParams.Ready =
        BackendFlowParams.Ready(
            linkedMapOf(
                KEY_PROFILE_CONF to profile.sourceText,
                KEY_PROFILE_BIN to profile.binary,
                KEY_PROFILE_RELEASE to profile.release,
            ),
        )

    /**
     * 可注入 seam：从 reference + 解析器构造参数（JVM 单测用）。
     *
     * @param reference profile reference（builtin:/uri:/空白）。
     * @param resolve reference→profile 解析器（生产=codec）。
     * @return Ready/Refused（失败具名 `cve43499:profile-invalid:*`）。
     */
    internal fun build(
        reference: String,
        resolve: (String) -> Resolved43499Profile,
    ): BackendFlowParams = runCatching { resolve(reference) }.fold(
        onSuccess = { profile -> fromResolved(profile) },
        onFailure = { problem ->
            BackendFlowParams.Refused(
                listOf(
                    "cve43499:profile-invalid:" +
                        (problem.message ?: problem::class.java.simpleName),
                ),
            )
        },
    )

    /**
     * host 侧从注入参数解码。
     *
     * @param env host 运行环境（params 已含 profile 三键）。
     * @return 解析结果；缺键/类型错/空值返回 null（入口具名失败）。
     */
    fun decode(env: FlowHostEnv): ParsedProfile? {
        val params = env.params
        val conf = params.getString(KEY_PROFILE_CONF) ?: return null
        val bin = params.getByteArray(KEY_PROFILE_BIN) ?: return null
        val release = params.getString(KEY_PROFILE_RELEASE) ?: return null
        if (conf.isBlank() || release.isBlank() || bin.isEmpty()) return null
        return ParsedProfile(conf = conf, bin = bin, release = release)
    }
}
