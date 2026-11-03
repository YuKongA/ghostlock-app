package com.ghostlock.app.shizuku

import android.content.Context
import android.os.Bundle
import android.os.ParcelFileDescriptor
import androidx.annotation.Keep
import com.ghostlock.app.shizuku.host.ShizukuBackendHost
import com.ghostlock.app.shizuku.host.ShizukuBackendHosts
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong

/**
 * **全 backend 共用的 Shizuku UserService 启动器/host**。
 *
 * Shizuku 通过 `bindUserService` 在 **uid 2000 / u:r:shell:s0 独立进程**（app_process）实例化本类；
 * 本类只做通用层：AIDL stub、backend host 分发、日志泵、日志 fd、生命周期。
 * backend 专用逻辑在 `ShizukuBackendHost` 实现（如 `Cve98260ShizukuHost`）。
 *
 * @note 不再存在任何 per-backend UserService/AIDL（2026-10-28 用户裁定：启动器必须共用）。
 * @note 未在 manifest 注册（Shizuku UserService 非 AMS 组件）；真机门禁 G8–G14 pending。
 */
class GhostlockUserService : IGhostlockUserService.Stub {

    private val pumping = AtomicBoolean(false)
    private val consumed = AtomicLong(0L)
    private var pumpThread: Thread? = null

    @Volatile
    private var host: ShizukuBackendHost? = null

    @Volatile
    private var callback: IGhostlockCallback? = null

    /** 默认构造器（Shizuku 必需）。 */
    constructor()

    /**
     * API v13+ 的 Context 构造器（Shizuku 优先尝试）；必须 @Keep。
     *
     * @param context 由 Shizuku 创建的 package context（不是有效 app 进程的 Context）。
     */
    @Keep
    constructor(context: Context) {
        /* 通用层当前不依赖 Context；保留构造器以满足 Shizuku v13 约定。 */
    }

    override fun setCallback(cb: IGhostlockCallback?) {
        callback = cb
    }

    override fun clearCallback() {
        callback = null
    }

    override fun prepare(backendId: String?, config: Bundle?, payload: ByteArray?): Int {
        if (backendId == null || config == null || payload == null) {
            return GENERIC_ERR_ARGS
        }
        val active = host
        if (active != null && active.state() != HOST_STATE_IDLE) {
            return GENERIC_ERR_BUSY
        }
        val created = ShizukuBackendHosts.create(backendId) ?: return GENERIC_ERR_UNSUPPORTED
        host = created
        return created.prepare(config, payload)
    }

    override fun start(): Int {
        val active = host ?: return -GENERIC_ERR_STATE
        val result = active.start()
        if (result > 0) startPump()
        return result
    }

    override fun getState(): Int = host?.state() ?: HOST_STATE_IDLE

    override fun readLog(offset: Long, maxBytes: Int): ByteArray =
        host?.readLog(offset, maxBytes) ?: ByteArray(0)

    override fun verify(): ByteArray = host?.verify() ?: ByteArray(0)

    override fun bootId(): String = host?.bootId() ?: ""

    override fun ping(): Int = 1

    override fun lastReason(): String = host?.lastReason() ?: ""

    override fun attachLog(logFd: ParcelFileDescriptor?) {
        if (logFd == null) return
        val raw = runCatching { logFd.detachFd() }.getOrElse { return }
        /* host 必须接管并关闭 raw fd（无论成败）；不支持时通用层直接丢弃（回退 AIDL onLog）。 */
        val attached = host?.attachLogFd(raw) ?: false
        if (!attached) {
            runCatching { android.os.ParcelFileDescriptor.adoptFd(raw).close() }
        }
    }

    override fun destroy() {
        stopPump()
        runCatching { host?.destroy() }
        host = null
        System.exit(0)
    }

    /** 通用日志泵：host 日志增量 → 回调（终端）+ host native 日志行（项目 log fd）。 */
    private fun startPump() {
        if (pumping.getAndSet(true)) return
        pumpThread = Thread({
            var carry = ""
            while (pumping.get()) {
                val active = host
                if (active == null) break
                val chunk = active.readLog(consumed.get(), MAX_CHUNK)
                if (chunk.isEmpty()) {
                    if (active.state() != HOST_STATE_RUNNING) break
                    runCatching { Thread.sleep(PUMP_INTERVAL_MS) }
                    continue
                }
                consumed.addAndGet(chunk.size.toLong())
                val text = carry + String(chunk, Charsets.UTF_8)
                val parts = text.split('\n')
                carry = parts.last()
                for (index in 0 until parts.size - 1) {
                    val line = parts[index].take(MAX_LINE)
                    if (line.isEmpty()) continue
                    active.logLine(2, line)
                    callback?.onLog(line)
                }
            }
        }, "gl-host-log-pump").apply { isDaemon = true; start() }
    }

    private fun stopPump() {
        pumping.set(false)
        pumpThread?.let { runCatching { it.join(PUMP_JOIN_MS) } }
        pumpThread = null
    }

    private companion object {
        /** 通用（非 backend 语义）返回码：仅表达 host 分发层错误。 */
        private const val GENERIC_ERR_ARGS = 1
        private const val GENERIC_ERR_BUSY = 3
        private const val GENERIC_ERR_STATE = 7
        private const val GENERIC_ERR_UNSUPPORTED = 9

        /** host 状态：IDLE/RUNNING（与 backend 状态值对齐；通用层只用于泵停止条件）。 */
        private const val HOST_STATE_IDLE = 0
        private const val HOST_STATE_RUNNING = 2

        private const val MAX_CHUNK = 64 * 1024
        private const val MAX_LINE = 512
        private const val PUMP_INTERVAL_MS = 500L
        private const val PUMP_JOIN_MS = 1000L
    }
}
