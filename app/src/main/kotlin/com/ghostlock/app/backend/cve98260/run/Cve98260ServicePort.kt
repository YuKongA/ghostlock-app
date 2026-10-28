package com.ghostlock.app.backend.cve98260.run

/**
 * CVE-2026-98260 远端原语端口（B2；app Flow 与 service-side client 的契约）。
 *
 * app `Flow98260Impl` 只依赖本接口（测试可注入 fake）：步骤/重试/超时/取消/终态
 * 全在 app（ADR-0022），实现（`Cve98260ServiceClient`）只做幂等原语转发与 AIDL 生命周期。
 * 本接口**不提供 kill/signal**；payload 的存活由 service 侧 `POSIX_SPAWN_SETSID` 保证。
 */
internal interface Cve98260ServicePort {
    /** service 状态码（后端稳定码；经公用 Shizuku host 透传）。 */
    object State {
        const val IDLE = 0
        const val PREPARED = 1
        const val RUNNING = 2
        const val TERMINAL = 3
        const val UNKNOWN = 4
    }

    /** service 稳定返回码（后端稳定码；经公用 Shizuku host 透传）。 */
    object Code {
        const val OK = 0
        const val ERR_ARGS = 1
        const val ERR_PREFLIGHT = 2
        const val ERR_BUSY = 3
        const val ERR_JOURNAL = 4
        const val ERR_STAGE = 5
        const val ERR_SPAWN = 6
        const val ERR_STATE = 7
        const val ERR_IO = 8
        const val ERR_UNSUPPORTED = 9
    }

    /**
     * 绑定并准备（service 预检 + 写 payload + journal installed）。
     *
     * @param profileHex `Cve98260Profile.toParamArg()`。
     * @param payload 静态 payload 字节（≤1 MiB-64 KiB）。
     * @param onLine 日志回调（可为 no-op）。
     * @return [Code]。
     */
    suspend fun prepare(
        profileHex: String,
        payload: ByteArray,
        finishScript: ByteArray,
        runId: String,
        helperPath: String,
        onLine: (String) -> Unit,
    ): Int

    /**
     * 启动 payload（journal launched → 公用 victim launcher → pid 存活；R1e）。
     *
     * @return >0=pid；<0=具名负码（`-Code.*`）。
     */
    suspend fun start(): Int

    /** @return [State]。 */
    suspend fun state(): Int

    /**
     * 读日志增量。
     *
     * @param offset 已消费偏移。
     * @param maxBytes 上限（≤64 KiB）。
     * @return 数据块；空=无新数据/EOF。
     */
    suspend fun readLog(offset: Long, maxBytes: Int): ByteArray

    /** @return UTF-8 `key=value` 行（独立验证事实；不抛异常，失败进 evidence）。 */
    suspend fun verify(): ByteArray

    /** @return 最近一次原语的具名 reason（OK 时为空串；稳定可检索）。 */
    suspend fun lastReason(): String

    /** @return service 读到的 boot_id；失败返回空串（app 以直读为权威）。 */
    suspend fun bootId(): String

    /**
     * 释放 AIDL 绑定。
     *
     * @param remove true=调用 `unbindUserService(remove=true)` 触发 destroy（仅正常 terminal 用）；
     *   false=只解绑（armed/不确定时用，不能触发 destroy）。
     */
    suspend fun disconnect(remove: Boolean)
}

/**
 * app 侧安全闸端口（B2）：唯一权威仍是 [com.ghostlock.app.data.cve98260.BootSafetyStore]；
 * Flow 只经本接口读写，避免 backend → data 反向依赖。
 */
internal interface Cve98260SafetyPort {
    /** @return 设备内核 release（`System.getProperty("os.version")`；未知 null）。 */
    fun kernelRelease(): String?

    /** @return 当前 boot_id；未知 null（调用方 fail-closed）。 */
    fun bootId(): String?

    /** @return 当前 store 是否已置 must_reboot/launch_pending。 */
    fun storeBlocked(): Boolean

    /**
     * 落 `launch_pending=true`（AIDL start 之前）。
     *
     * @param bootId 当前 boot_id。
     * @return false=持久化失败（不得 launch）。
     */
    fun markLaunchPending(bootId: String?): Boolean

    /**
     * 落终态（成功必须 mustReboot=true）。
     *
     * @param bootId 当前 boot_id。
     * @param mustReboot 是否禁止本 boot 再运行。
     * @param outcome `ok`/`failed`/`indeterminate`/`persist-failed`。
     * @param reason 具名 reason。
     * @return false=持久化失败（Flow 必须升级为 persist-failed 语义）。
     */
    fun markTerminal(bootId: String?, mustReboot: Boolean, outcome: String, reason: String?): Boolean
}
