package com.ghostlock.app.core.victim

import java.io.File

/**
 * 公用 victim 脚本启动器（backend-agnostic 机制层）。
 *
 * 职责：`VictimSession.start`（spawn 通用 helper）→ `execScript(启动脚本)` → 校验
 * exit/timeout → **回调 backend 的结果探针**（如读 pid 文件）→ `close(helper)`。
 *
 * 通用层**不解释** backend 脚本内容/argv/pid 语义；脚本由 backend 生成并在脚本内把
 * payload 后台 detach（这是 backend 约定，不是通用层能保证的）。
 *
 * 安全：`execScript` 超时只会终止**启动脚本**（helper 语义），不会触及 payload；
 * 本类不提供 kill/timeout payload 的入口。
 *
 * @param sessionFactory 会话 seam（生产=`core/victim` 真会话；测试注入 fake）。
 * @param scriptWriter 写脚本 seam（生产=写 0700 文件）。
 * @note 真机门禁 G8–G14 pending。
 */
internal class VictimScriptLauncher(
    private val sessionFactory: (VictimSessionConfig) -> VictimScriptSession = { config ->
        CoreVictimScriptSession(config)
    },
    private val scriptWriter: (String, ByteArray) -> Boolean = ::writeExecutableScript,
) {
    /**
     * 启动 helper 并执行启动脚本，然后调用 [onScriptDone] 取 backend 结果。
     *
     * @param config 会话配置（helper 路径/argv/capabilities/raw 容量；由 backend 提供）。
     * @param scriptPath 启动脚本路径（backend 选择；通用层只写文件）。
     * @param script 启动脚本字节（非空）。
     * @param execTimeoutMs helper 执行脚本的窗口（>0；超时只终止脚本）。
     * @param onScriptDone 脚本成功后的 backend 结果探针（如读 pid/存活）；返回 backend 结果。
     * @return backend 结果；脚本/会话失败返回具名 `victim-script:*`。
     */
    fun launch(
        config: VictimSessionConfig,
        scriptPath: String,
        script: ByteArray,
        execTimeoutMs: Long,
        onScriptDone: () -> VictimResult<Int>,
    ): VictimResult<Int> {
        if (scriptPath.isBlank() || script.isEmpty() || execTimeoutMs <= 0L) {
            return VictimResult.failed(VictimError.INVALID_ARGUMENT.token)
        }
        if (!scriptWriter(scriptPath, script)) {
            return VictimResult.failed("victim-script:write")
        }
        val session = runCatching { sessionFactory(config) }.getOrElse { problem ->
            return VictimResult.failed("victim-script:session:" + problem::class.java.simpleName)
        }
        try {
            val started = runCatching { session.start() }.getOrElse { problem ->
                return VictimResult.failed("victim-script:start:" + problem::class.java.simpleName)
            }
            when (started) {
                is VictimResult.Failed -> return started
                is VictimResult.Ok -> Unit
            }
            val executed = runCatching { session.execScript(scriptPath, execTimeoutMs) }
                .getOrElse { problem ->
                    return VictimResult.failed("victim-script:exec:" + problem::class.java.simpleName)
                }
            when (executed) {
                is VictimResult.Failed -> return executed
                is VictimResult.Ok -> {
                    val result = executed.value
                    if (result.timedOut) return VictimResult.failed("victim-script:timeout")
                    if (result.helperErrno != 0 || result.exitCode != 0) {
                        return VictimResult.failed(
                            "victim-script:exit:" + result.exitCode + ":" + result.helperErrno,
                        )
                    }
                }
            }
            return onScriptDone()
        } finally {
            /* 只回收 helper（脚本已退出；payload detach 由脚本负责），不碰 payload。 */
            runCatching { session.close() }
        }
    }

    /** 会话 seam（生产=[CoreVictimScriptSession]；宿主测试可注入 fake）。 */
    internal interface VictimScriptSession {
        /** @return Ok=helper 已启动。 */
        fun start(): VictimResult<Unit>

        /** @return helper `execScript` 结果（脚本执行，不是 payload 生命周期）。 */
        fun execScript(scriptPath: String, timeoutMs: Long): VictimResult<VictimExecResult>

        /** 回收 helper（可 kill helper；**不碰 payload**）。 */
        fun close()
    }

    /** 生产 seam：`core/victim.VictimSession` + 通用 helper。 */
    internal class CoreVictimScriptSession(
        private val config: VictimSessionConfig,
    ) : VictimScriptSession {
        private var session: VictimSession? = null

        override fun start(): VictimResult<Unit> {
            if (session != null) return VictimResult.failed(VictimError.NOT_STARTED.token)
            val created = VictimSession(VictimNativeJni, config)
            session = created
            return when (val started = created.start()) {
                is VictimResult.Failed -> started
                is VictimResult.Ok -> VictimResult.Ok(Unit)
            }
        }

        override fun execScript(scriptPath: String, timeoutMs: Long): VictimResult<VictimExecResult> {
            val active = session ?: return VictimResult.failed(VictimError.NOT_STARTED.token)
            return active.execScript(
                VictimProtocol.ExecScriptRequest(
                    scriptPath = scriptPath,
                    argv = emptyList(),
                    env = emptyList(),
                    timeoutMs = timeoutMs,
                ),
            )
        }

        override fun close() {
            runCatching { session?.close() }
            session = null
        }
    }

    private companion object {
        /** 脚本写 0700（helper sh 可读）。 */
        private fun writeExecutableScript(path: String, bytes: ByteArray): Boolean = runCatching {
            val file = File(path)
            file.writeBytes(bytes)
            file.setExecutable(true, false) && file.length() == bytes.size.toLong()
        }.getOrDefault(false)
    }
}
