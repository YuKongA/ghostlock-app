package com.ghostlock.app.backend.cve98260.service

import android.os.Bundle
import com.ghostlock.app.backend.cve98260.run.Cve98260ServicePort
import com.ghostlock.app.jni.Native98260Jni
import com.ghostlock.app.shizuku.host.ShizukuBackendHost
import com.ghostlock.app.shizuku.host.ShizukuBackendHosts

/**
 * CVE-2026-98260 在**公用 Shizuku host** 中的处理实现（R1e 结构修正）。
 *
 * 结构：`GhostlockUserService`（所有 backend 共用的 UserService/AIDL/日志泵）
 * → 本 host（98260 专用执行器适配）→ `Cve98260RemoteExecutor`（幂等原语）。
 * **不存在 98260 专用 UserService/AIDL**：Shizuku 启动器只有一份。
 *
 * @note host 只在 service 进程（uid 2000 / shell 域）内使用；终态权威仍在 app Flow。
 */
internal class Cve98260ShizukuHost : ShizukuBackendHost {
    private val executor = Cve98260RemoteExecutor()

    override val backendId: String = ShizukuBackendHosts.CVE_98260

    override fun prepare(config: Bundle, payload: ByteArray): Int {
        val profileHex = config.getString(KEY_PROFILE_HEX) ?: return Cve98260ServicePort.Code.ERR_ARGS
        val finishScript = config.getByteArray(KEY_FINISH_SCRIPT)
            ?: return Cve98260ServicePort.Code.ERR_ARGS
        val runId = config.getString(KEY_RUN_ID) ?: return Cve98260ServicePort.Code.ERR_ARGS
        val helperPath = config.getString(KEY_HELPER_PATH) ?: return Cve98260ServicePort.Code.ERR_ARGS
        return executor.prepare(profileHex, payload, finishScript, runId, helperPath)
    }

    override fun start(): Int = executor.start()

    override fun state(): Int = executor.state()

    override fun readLog(offset: Long, maxBytes: Int): ByteArray = executor.readLog(offset, maxBytes)

    override fun verify(): ByteArray = executor.verify()

    override fun lastReason(): String = executor.lastReason()

    override fun bootId(): String = executor.bootId()

    override fun attachLogFd(rawFd: Int): Boolean = runCatching {
        Native98260Jni.attachLogFd(rawFd)
        true
    }.getOrElse {
        /* JNI 接管失败时 raw fd 仍归 host：关闭，避免泄漏。 */
        runCatching { android.os.ParcelFileDescriptor.adoptFd(rawFd).close() }
        false
    }

    override fun logLine(level: Int, line: String) {
        runCatching { Native98260Jni.logLine(level, line) }
    }

    override fun destroy() {
        runCatching { Native98260Jni.detachLogFd() }
        executor.destroy()
    }

    /** app→service 的 98260 私有配置键（Bundle；通用层不解释）。 */
    internal companion object {
        const val KEY_PROFILE_HEX: String = "cve98260.profileHex"
        const val KEY_FINISH_SCRIPT: String = "cve98260.finishScript"
        const val KEY_RUN_ID: String = "cve98260.runId"
        const val KEY_HELPER_PATH: String = "cve98260.helperPath"
    }
}
