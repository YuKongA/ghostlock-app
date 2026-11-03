package com.ghostlock.app.shizuku.host

import android.content.Context
import android.os.Bundle

/**
 * 公用 Shizuku host 的 backend 处理接口（所有 backend 共用同一 UserService 进程）。
 *
 * 分层：`GhostlockUserService`（AIDL/绑定/日志泵/生命周期）→ 本接口 → backend 专用执行器。
 * 通用层**不解释** backend 状态/返回码；只做幂等转发与日志/回调。
 *
 * @note host 只在 service 进程内使用；app Flow 仍是唯一 run 权威。
 * @note 真机门禁 G8–G14 pending（UserService 进程加载 backend native/victim 依赖）。
 */
internal interface ShizukuBackendHost {
    /** 稳定 wire id（与 app 侧 `ShizukuBackendHosts` 一致）。 */
    val backendId: String

    /**
     * 准备（host 自解释 config 键）。
     *
     * @param config backend 私有配置。
     * @param payload payload 字节。
     * @return backend 稳定码（0=OK）。
     */
    fun prepare(config: Bundle, payload: ByteArray): Int

    /** @return pid/负码（backend 解释）。 */
    fun start(): Int

    /** @return backend 状态码。 */
    fun state(): Int

    /** @return 日志增量（不删除/不轮转）。 */
    fun readLog(offset: Long, maxBytes: Int): ByteArray

    /** @return 独立验证证据（UTF-8 key=value 行）。 */
    fun verify(): ByteArray

    /** @return 最近具名 reason。 */
    fun lastReason(): String

    /** @return boot_id；失败空串。 */
    fun bootId(): String

    /**
     * 接上 app 日志 fd。
     *
     * @param rawFd detached raw fd.
     * @return true=host 接管并关闭 fd；false=通用层关闭 fd 并回退 AIDL onLog。
     */
    fun attachLogFd(rawFd: Int): Boolean

    /**
     * 写一行到 backend 的原生日志管线（若支持；不支持 no-op）。
     *
     * @param level 0..4（verbose..error）。
     * @param line 单行正文。
     */
    fun logLine(level: Int, line: String)

    /**
     * 释放该 host 自己拥有的资源，由 Shizuku destroy 约定调用。
     *
     * @note Detached payload policy is backend-specific（历史 backend 语义保留；
     *   新进程 FlowHost 为独立进程，不在此接口内）。
     */
    fun destroy()
}

/**
 * backend host 注册表（公用 Shizuku 启动器的唯一分发点）。
 *
 * @note 当前无内置 host（legacy 98260 host 已移除）；未知 id 一律 fail-closed 返回 null。
 */
internal object ShizukuBackendHosts {
    /**
     * @param backendId 稳定 wire id。
     * @return 新 host 实例；未知 id 返回 null（fail-closed）。
     */
    fun create(backendId: String, context: Context? = null): ShizukuBackendHost? = null
}
