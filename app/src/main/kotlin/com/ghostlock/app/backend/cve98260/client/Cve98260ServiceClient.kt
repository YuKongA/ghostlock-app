package com.ghostlock.app.backend.cve98260.client

import android.content.Context
import android.os.Bundle
import com.ghostlock.app.backend.cve98260.run.Cve98260ServicePort
import com.ghostlock.app.backend.cve98260.service.Cve98260ShizukuHost
import com.ghostlock.app.shizuku.ShizukuBackendClient
import com.ghostlock.app.shizuku.host.ShizukuBackendHosts

/**
 * 98260 的 service 端口适配器（B2/R1e 结构修正）。
 *
 * 绑定/日志 fd/AIDL 全部走**公用** `ShizukuBackendClient`（所有 backend 共用同一个
 * `GhostlockUserService` 启动器）；本类只把 98260 参数打包成 host 私有 Bundle 并透传返回码。
 *
 * @note 不再有 98260 专用 UserService/AIDL（2026-10-28 用户裁定）。
 */
internal class Cve98260ServiceClient(context: Context) : Cve98260ServicePort {
    private val client = ShizukuBackendClient(context, ShizukuBackendHosts.CVE_98260)

    /** @return Shizuku 可用性具名原因（公用检查）。 */
    internal fun availabilityReasons(): List<String> = client.availabilityReasons()

    /** @return true=日志 fd 已接上公用 host。 */
    internal fun isLogAttached(): Boolean = client.isLogAttached()

    override suspend fun prepare(
        profileHex: String,
        payload: ByteArray,
        finishScript: ByteArray,
        runId: String,
        helperPath: String,
        onLine: (String) -> Unit,
    ): Int {
        val config = Bundle().apply {
            putString(Cve98260ShizukuHost.KEY_PROFILE_HEX, profileHex)
            putByteArray(Cve98260ShizukuHost.KEY_FINISH_SCRIPT, finishScript)
            putString(Cve98260ShizukuHost.KEY_RUN_ID, runId)
            putString(Cve98260ShizukuHost.KEY_HELPER_PATH, helperPath)
        }
        return client.prepare(config, payload, onLine)
    }

    override suspend fun start(): Int = client.start()

    override suspend fun state(): Int = client.state()

    override suspend fun readLog(offset: Long, maxBytes: Int): ByteArray =
        client.readLog(offset, maxBytes)

    override suspend fun verify(): ByteArray = client.verify()

    override suspend fun lastReason(): String = client.lastReason()

    override suspend fun bootId(): String = client.bootId()

    override suspend fun disconnect(remove: Boolean) {
        client.disconnect(remove)
    }
}
