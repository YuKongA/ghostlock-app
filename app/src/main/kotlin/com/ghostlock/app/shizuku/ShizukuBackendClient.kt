package com.ghostlock.app.shizuku

import android.content.ComponentName
import android.content.Context
import android.content.pm.PackageManager
import android.os.Bundle
import android.os.IBinder
import android.os.ParcelFileDescriptor
import com.ghostlock.app.BuildConfig
import com.ghostlock.app.log.LogFile
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import rikka.shizuku.Shizuku
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit

/**
 * **公用 Shizuku backend 客户端**（所有 backend 共用同一个 UserService 启动器）。
 *
 * 只做绑定生命周期 + AIDL 原语转发 + 日志 fd 接入；backend 语义在 host/Flow。
 * 通用层不解释 backend 返回码（透传）；未注册 backendId ⇒ host 侧 fail-closed。
 *
 * @param context 应用上下文（包名/日志 fd）。
 * @param backendId 稳定 wire id（`ShizukuBackendHosts` 常量）。
 * @note 真机门禁 G8–G14 pending；绑定/回调失败以具名负码返回。
 */
internal class ShizukuBackendClient(
    context: Context,
    private val backendId: String,
) {
    private val appContext: Context = context.applicationContext
    private val args: Shizuku.UserServiceArgs = Shizuku.UserServiceArgs(
        ComponentName(appContext.packageName, SERVICE_CLASS),
    )
        .daemon(false)
        .version(BuildConfig.VERSION_CODE)
        .tag(TAG)
        .processNameSuffix(PROCESS_NAME_SUFFIX)

    private val lock = Any()
    private var latch = CountDownLatch(1)
    private var service: IGhostlockUserService? = null
    private var connected = false
    private var disconnected = false

    @Volatile
    private var logAttached = false

    private var lineSink: ((String) -> Unit)? = null

    private val connection = object : android.content.ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
            synchronized(lock) {
                service = binder?.let { IGhostlockUserService.Stub.asInterface(it) }
                connected = service != null
                disconnected = false
            }
            latch.countDown()
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            synchronized(lock) {
                connected = false
                disconnected = true
                logAttached = false
                service = null
            }
        }
    }

    private val callback = object : IGhostlockCallback.Stub() {
        override fun onLog(line: String?) {
            lineSink?.invoke(line ?: "")
        }

        override fun onStatus(state: Int) {
            /* 状态由 Flow 通过 state() 拉取；回调只作诊断。 */
        }

        override fun onDropped(count: Int) {
            lineSink?.invoke("GHOSTLOCK_HOST_CALLBACK_DROPPED=$count")
        }

        override fun onComplete(code: Int) {
            lineSink?.invoke("GHOSTLOCK_HOST_CALLBACK_COMPLETE=$code")
        }
    }

    /** @return 公共 Shizuku 可用性具名原因；空=可用。 */
    internal fun availabilityReasons(): List<String> {
        val reasons = mutableListOf<String>()
        if (runCatching { Shizuku.pingBinder() }.getOrDefault(false) != true) {
            reasons += "shizuku:preflight:not-running"
            return reasons
        }
        val granted = runCatching {
            Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED
        }.getOrDefault(false)
        if (!granted) reasons += "shizuku:preflight:not-granted"
        val uid = runCatching { Shizuku.getUid() }.getOrNull()
        if (uid != 2000) reasons += "shizuku:preflight:uid:" + (uid?.toString() ?: "unknown")
        val domain = runCatching { Shizuku.getSELinuxContext() }.getOrNull()
        if (domain?.contains("u:r:shell:s0") != true) {
            reasons += "shizuku:preflight:context:" + (domain ?: "unknown")
        }
        return reasons
    }

    /** @return true=日志 fd 已接上 host。 */
    internal fun isLogAttached(): Boolean = logAttached

    /**
     * @param config backend 私有 Bundle（键由 backend 定义）。
     * @param payload payload 字节。
     * @param onLine 日志行回调。
     * @return host 返回码（透传）。
     */
    internal suspend fun prepare(
        config: Bundle,
        payload: ByteArray,
        onLine: (String) -> Unit,
    ): Int = withContext(Dispatchers.IO) {
        val binder = ensureService() ?: return@withContext -ERR_IO
        lineSink = onLine
        attachLog(binder)
        runCatching {
            binder.setCallback(callback)
            binder.prepare(backendId, config, payload)
        }.getOrElse { -ERR_IO }
    }

    internal suspend fun start(): Int = withContext(Dispatchers.IO) {
        val binder = ensureService() ?: return@withContext -ERR_IO
        runCatching { binder.start() }.getOrElse { -ERR_IO }
    }

    internal suspend fun state(): Int = withContext(Dispatchers.IO) {
        val binder = ensureService() ?: return@withContext ERR_IO
        runCatching { binder.getState() }.getOrElse { ERR_IO }
    }

    internal suspend fun readLog(offset: Long, maxBytes: Int): ByteArray =
        withContext(Dispatchers.IO) {
            val binder = ensureService() ?: return@withContext ByteArray(0)
            runCatching { binder.readLog(offset, maxBytes) ?: ByteArray(0) }
                .getOrElse { ByteArray(0) }
        }

    internal suspend fun verify(): ByteArray = withContext(Dispatchers.IO) {
        val binder = ensureService() ?: return@withContext ByteArray(0)
        runCatching { binder.verify() ?: ByteArray(0) }.getOrElse { ByteArray(0) }
    }

    internal suspend fun lastReason(): String = withContext(Dispatchers.IO) {
        val binder = ensureService() ?: return@withContext ""
        runCatching { binder.lastReason() ?: "" }.getOrDefault("")
    }

    internal suspend fun bootId(): String = withContext(Dispatchers.IO) {
        val binder = ensureService() ?: return@withContext ""
        runCatching { binder.bootId() ?: "" }.getOrDefault("")
    }

    internal suspend fun disconnect(remove: Boolean) {
        withContext(Dispatchers.IO) {
            synchronized(lock) { lineSink = null; logAttached = false }
            runCatching { Shizuku.unbindUserService(args, connection, remove) }
            synchronized(lock) {
                service = null
                connected = false
            }
        }
    }

    /** 把 app 日志 fd 的 dup 传给公用 host（项目 log 管线）。 */
    private fun attachLog(binder: IGhostlockUserService) {
        if (logAttached) return
        val appFd = LogFile.fd
        if (appFd < 0) return
        val descriptor = duplicateFd(appFd) ?: return
        try {
            binder.attachLog(descriptor)
            logAttached = true
        } catch (_: Throwable) {
            logAttached = false
        } finally {
            runCatching { descriptor.close() }
        }
    }

    /** 复制 app 日志 fd（PFD.dup；不改 LogFile 原 fd 所有权）。 */
    private fun duplicateFd(fd: Int): ParcelFileDescriptor? = runCatching {
        val field = java.io.FileDescriptor::class.java.getDeclaredField("descriptor")
        field.isAccessible = true
        val source = java.io.FileDescriptor()
        field.setInt(source, fd)
        ParcelFileDescriptor.dup(source)
    }.getOrNull()

    /** 建立/复用绑定；失败 null。 */
    private fun ensureService(): IGhostlockUserService? {
        synchronized(lock) {
            service?.let { if (connected) return it }
        }
        if (runCatching { Shizuku.pingBinder() }.getOrDefault(false) != true) return null
        val granted = runCatching {
            Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED
        }.getOrDefault(false)
        if (!granted) return null
        latch = CountDownLatch(1)
        runCatching { Shizuku.bindUserService(args, connection) }.getOrElse { return null }
        val ok = runCatching { latch.await(CONNECT_TIMEOUT_MS, TimeUnit.MILLISECONDS) }
            .getOrDefault(false)
        if (!ok) return null
        return synchronized(lock) { if (connected) service else null }
    }

    private companion object {
        /** 公用 UserService 类全路径（与 `GhostlockUserService` 一致；不写 manifest）。 */
        private const val SERVICE_CLASS = "com.ghostlock.app.shizuku.GhostlockUserService"

        /** 稳定 tag（R8 后类名不稳定）。 */
        private const val TAG = "ghostlock-host"

        /** 进程名后缀：`<pkg>:shell`。 */
        private const val PROCESS_NAME_SUFFIX = "shell"

        /** 绑定窗口（ms）。 */
        private const val CONNECT_TIMEOUT_MS = 5000L

        /** 通用 I/O 负码（与 `Cve98260ServicePort.Code.ERR_IO` 对齐；通用层不再定义 backend 码）。 */
        private const val ERR_IO = 8
    }
}
