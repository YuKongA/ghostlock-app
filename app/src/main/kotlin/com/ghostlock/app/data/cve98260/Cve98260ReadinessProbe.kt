package com.ghostlock.app.data.cve98260

import android.content.Context
import com.ghostlock.app.backend.cve98260.payload.Cve98260ExploitPayload
import com.ghostlock.app.core.preflight.Cve98260Preflight
import java.io.File

/**
 * CVE-2026-98260 readiness 后台探针（B3）。
 *
 * readiness 是**同步声明**，不能直接做 binder/IO；本对象在后台（或缓存过期时）计算
 * 一个不可变快照，`BackendSpec.readiness()` 只读快照（fail-closed：未探测/未知=NotReady）。
 *
 * 只做 **app 可见输入事实**（profile/payload/BootSafetyStore/boot_id）；执行体 uid/模式
 * 由 UI 按 backend 声明检查（backend 不自检）。内核侧事实（tracefs/slab//proc/modules）
 * 由 service `prepare` 重跑（delta §F），app 不读 /proc/modules。
 *
 * @note 真机门禁 G8 pending；binder/IO 异常一律进 reasons，不外抛。
 */
internal object Cve98260ReadinessProbe {
    /** freshness：超过该毫秒数再次读取时刷新。 */
    private const val STALE_MS: Long = 2000L

    /** Shizuku 管理器包名（manifest `<queries>` 已声明）。 */
    private const val SHIZUKU_PACKAGE: String = "moe.shizuku.privileged.api"

    /** 一次探测的不可变快照。 */
    internal data class Snapshot(
        val ready: Boolean,
        val reasons: List<String>,
        val computedMs: Long,
        val payloadReady: Boolean = false,
        val payloadIdentity: String? = null,
    )

    @Volatile
    private var appContext: Context? = null

    @Volatile
    private var snapshot: Snapshot = Snapshot(
        ready = false,
        reasons = listOf("cve98260:probe:not-run"),
        computedMs = 0L,
    )

    /**
     * 绑定 app 上下文并立即探测（`GhostLockApp.onCreate` 调用）。
     *
     * @param context 应用上下文。
     */
    internal fun attach(context: Context) {
        appContext = context.applicationContext
        refresh()
    }

    /**
     * 刷新快照（IO/binder；调用方可放后台线程）。
     *
     * @return 新快照。
     */
    internal fun refresh(): Snapshot {
        val computed = probe()
        snapshot = computed
        return computed
    }

    /**
     * 读取快照；过期时同步刷新（binder 调用，ms 级）。
     *
     * @return 当前快照。
     */
    internal fun snapshot(): Snapshot {
        val current = snapshot
        if (System.currentTimeMillis() - current.computedMs <= STALE_MS) return current
        return refresh()
    }

    private fun probe(): Snapshot {
        val context = appContext
            ?: return Snapshot(false, listOf("cve98260:probe:not-attached"), System.currentTimeMillis())
        val profile = runCatching { Cve98260ProfileConfig.load(context) }.getOrNull()
        val artifact = Cve98260ExploitPayload.load(context)
        val store = runCatching { BootSafetyStore.load(context) }.getOrDefault(BootSafetyStore.EMPTY)
        val bootId = runCatching {
            File("/proc/sys/kernel/random/boot_id").readText().trim().ifEmpty { null }
        }.getOrNull()
        val release = runCatching { System.getProperty("os.version") }.getOrNull()
        val facts = Cve98260Preflight.AppFacts(
            profileParsed = profile != null,
            releaseMatchesDevice = profile?.matchesDevice(release) == true,
            payloadReady = artifact is Cve98260ExploitPayload.Load.Loaded,
            storeBlocked = store.isBlocked(),
            bootIdKnown = bootId != null,
        )
        val result = Cve98260Preflight.evaluateApp(facts)
        val loaded = artifact as? Cve98260ExploitPayload.Load.Loaded
        return Snapshot(
            ready = result.ok,
            reasons = result.reasons,
            computedMs = System.currentTimeMillis(),
            payloadReady = loaded != null,
            payloadIdentity = loaded?.payload?.identityText(),
        )
    }
}
