package com.ghostlock.app.backend.host

import com.ghostlock.app.core.flow.FlowHostParams

/**
 * host 注入参数的保留键与校验/融合（B4a；附录 `flow-process-launcher-annex-b4a.md` §1）。
 *
 * 分工：
 * - 调用方（UI/launcher）只能传业务参数，`host.*` 一律拒绝 `flow-params:reserved-key:<key>`；
 * - host 在 prepare 阶段把自己的运行时事实（uid/pid/domain/run_dir/native_lib_dir/log_path）
 *   以 `host.*` 覆盖注入；调用方即使塞入同键也不生效；
 * - 注入后仍走 [FlowHostParams] 白名单/预算（≤256 KiB）。
 *
 * @note 纯 JVM；无 Android 依赖。
 * @note 阶段：B4a（迁移期单权威；B5c 收口）。
 */
object FlowEntryParams {
    /** host 保留前缀。 */
    const val RESERVED_PREFIX: String = "host."

    /** host 进程 uid。 */
    const val KEY_HOST_UID: String = "host.uid"

    /** host 进程 pid。 */
    const val KEY_HOST_PID: String = "host.pid"

    /** host 进程 SELinux domain。 */
    const val KEY_HOST_DOMAIN: String = "host.domain"

    /** 本次 run 的 host 目录。 */
    const val KEY_HOST_RUN_DIR: String = "host.run_dir"

    /** host 可见的 native lib 目录。 */
    const val KEY_HOST_NATIVE_LIB_DIR: String = "host.native_lib_dir"

    /** 本次 run 日志文件路径（run_dir 下）。 */
    const val KEY_HOST_LOG_PATH: String = "host.log_path"

    /**
     * 校验调用方参数不得包含保留键。
     *
     * @param values 调用方提供的参数。
     * @throws IllegalArgumentException 命中 `host.*`：
     *   `flow-params:reserved-key:<key>`（首个命中键）。
     */
    fun validateUserParams(values: Map<String, Any?>) {
        val reserved = values.keys.firstOrNull { it.startsWith(RESERVED_PREFIX) }
        if (reserved != null) {
            throw IllegalArgumentException("flow-params:reserved-key:$reserved")
        }
    }

    /**
     * 把 host 运行时事实注入已校验参数（host 值覆盖调用方同键）。
     *
     * @param base 调用方参数（其中的 `host.*` 会被丢弃，不生效）。
     * @param host host 侧参数（键必须全部以 [RESERVED_PREFIX] 开头）。
     * @return 注入后的新 [FlowHostParams]（重新走白名单/预算）。
     * @throws IllegalArgumentException host 键非法/预算超限：
     *   `flow-params:bad-host-key:<key>` 或 FlowHostParams 的 `flow-params:*`。
     */
    fun injectHost(base: FlowHostParams, host: Map<String, Any?>): FlowHostParams {
        val badHostKey = host.keys.firstOrNull { !it.startsWith(RESERVED_PREFIX) }
        if (badHostKey != null) {
            throw IllegalArgumentException("flow-params:bad-host-key:$badHostKey")
        }
        val cleaned = LinkedHashMap<String, Any?>()
        for ((key, value) in base.values()) {
            if (!key.startsWith(RESERVED_PREFIX)) cleaned[key] = value
        }
        cleaned.putAll(host)
        return FlowHostParams.of(cleaned)
    }
}
