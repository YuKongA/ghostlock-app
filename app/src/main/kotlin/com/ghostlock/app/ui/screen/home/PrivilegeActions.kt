package com.ghostlock.app.ui.screen.home

import android.content.Context
import android.content.pm.PackageManager
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import com.ghostlock.app.R
import com.ghostlock.app.ui.component.dialog.PrivilegeAvailability
import com.ghostlock.app.ui.component.dialog.PrivilegeDialog as PrivilegeDialogComponent
import com.ghostlock.app.ui.component.dialog.PrivilegeDialogPage
import com.ghostlock.app.ui.component.dialog.PrivilegeDialogState
import com.ghostlock.app.ui.component.dialog.PrivilegeExecutorRow
import rikka.shizuku.Shizuku

/**
 *  权限状态（2026-10-18 接线）。
 *
 *  主页那一行显示当前执行体（app/shizuku）与 uid；点箭头进子菜单：
 *   · 解释两种执行体各自的权限边界
 *   · 向 Shizuku 请求授权（shell uid 2000）
 *
 *  手动 adb 授权入口已删除（2026-10-19 用户裁定）：Shizuku 未就绪时只提示安装/启动，
 *  不再引导用户去电脑上跑 adb 脚本。
 *
 *  task-28 组件化：本文件只保留非 UI 逻辑（执行体偏好、Shizuku 授权请求/listener/onResume/
 *  短轮询）；提示框 UI（执行体列表/模式切换/文案）全部在
 *  `ui/component/dialog/PrivilegeDialog.kt` 渲染。
 */
enum class ExecutorMode { App, Shizuku }

/** 当前选用的执行体（用户可在子菜单里切回 app）。 */
object ExecutorPreference {
    private const val PREFS = "ghostlock.privilege"
    private const val KEY = "executor_mode"

    var mode by mutableStateOf(ExecutorMode.App)

    fun load(context: Context) {
        mode = if (context.getSharedPreferences(PREFS, 0).getString(KEY, "app") == "shizuku") {
            ExecutorMode.Shizuku
        } else {
            ExecutorMode.App
        }
    }

    fun set(context: Context, value: ExecutorMode) {
        mode = value
        context.getSharedPreferences(PREFS, 0).edit()
            .putString(KEY, if (value == ExecutorMode.Shizuku) "shizuku" else "app")
            .apply()
    }
}

/** Shizuku 是否已安装/可用（binder 可 ping）。 */
private fun shizukuAvailable(): Boolean = runCatching { Shizuku.pingBinder() }.getOrDefault(false)

/**
 * 构造当前执行体行（只给事实，文案由共享组件本地化）。
 *
 * @param context 应用上下文。
 * @param granted Shizuku 是否已授权。
 * @return app（应用 uid）+ shizuku（shell uid 2000）两行。
 */
private fun executorRows(context: Context, granted: Boolean): List<PrivilegeExecutorRow> {
    val appUid = context.applicationInfo.uid
    val shizukuAvailability = when {
        !shizukuAvailable() -> PrivilegeAvailability.UNAVAILABLE
        granted -> PrivilegeAvailability.GRANTED
        else -> PrivilegeAvailability.DENIED
    }
    return listOf(
        PrivilegeExecutorRow(
            label = "app",
            uid = appUid,
            availability = PrivilegeAvailability.APP,
            available = true,
        ),
        PrivilegeExecutorRow(
            label = "shizuku",
            uid = 2000,
            availability = shizukuAvailability,
            available = granted,
        ),
    )
}

private fun shizukuGranted(): Boolean = runCatching {
    Shizuku.pingBinder() && Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED
}.getOrDefault(false)

/**
 *  当前执行体的副标题，例如 "app · uid 10231" 或 "shizuku · uid 2000"。
 *
 *  Shizuku 授权/取消会通过 listener 触发重组；进程本身仍以 app uid 运行，
 *  这里只反映当前可用的执行通道（2026-10-18 用户反馈）。
 */
@Composable
fun currentExecutorLabel(): String {
    val context = LocalContext.current
    val lifecycleOwner = androidx.lifecycle.compose.LocalLifecycleOwner.current
    var granted by remember { mutableStateOf(shizukuGranted()) }
    DisposableEffect(lifecycleOwner) {
        /* 从 Shizuku 授权界面回来时刷新（onResume），授权回调也刷新。 */
        val observer = androidx.lifecycle.LifecycleEventObserver { _, event ->
            if (event == androidx.lifecycle.Lifecycle.Event.ON_RESUME) granted = shizukuGranted()
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        val listener = Shizuku.OnRequestPermissionResultListener { _, _ -> granted = shizukuGranted() }
        runCatching { Shizuku.addRequestPermissionResultListener(listener) }
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(observer)
            runCatching { Shizuku.removeRequestPermissionResultListener(listener) }
        }
    }
    /* ShizukuProvider 的 binder 是异步到达的，启动后短轮询几次，避免首帧误判为 app。 */
    LaunchedEffect(Unit) {
        repeat(6) {
            granted = shizukuGranted()
            kotlinx.coroutines.delay(300)
        }
    }
    LaunchedEffect(Unit) { ExecutorPreference.load(context) }
    return if (granted && ExecutorPreference.mode == ExecutorMode.Shizuku) {
        stringResource(R.string.privilege_subtitle_format, "shizuku", 2000)
    } else {
        stringResource(R.string.privilege_subtitle_format, "app", context.applicationInfo.uid)
    }
}

/** 发起 Shizuku 授权请求；返回是否成功把请求交给 Shizuku。 */
private fun requestShizuku(): Boolean = runCatching {
    if (!Shizuku.pingBinder()) return false
    if (Shizuku.isPreV11()) return false
    if (Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED) return true
    Shizuku.requestPermission(0)
    true
}.getOrDefault(false)

/**
 * 权限状态子菜单（逻辑壳）。
 *
 * 本函数只负责 Shizuku 授权状态（listener + onResume + 短轮询）与页面切换行为；
 * 弹窗 UI（执行体列表/说明/Back）全部委托共享组件
 * `ui/component/dialog/PrivilegeDialog`（task-28 组件化）。
 *
 * @param show 是否展示。
 * @param onDismiss 关闭回调。
 * @param miuix 是否 Miuix 变体（与主页 UI 套件一致）。
 */
@Composable
fun PrivilegeDialog(show: Boolean, onDismiss: () -> Unit, miuix: Boolean) {
    if (!show) return
    var page by remember(show) { mutableStateOf(PrivilegeDialogPage.OVERVIEW) }
    val context = LocalContext.current
    /* 授权结果异步回来：listener + onResume + 短轮询，保证卡片立即从灰变可点。 */
    var granted by remember { mutableStateOf(shizukuGranted()) }
    val lifecycleOwner = androidx.lifecycle.compose.LocalLifecycleOwner.current
    DisposableEffect(lifecycleOwner) {
        val observer = androidx.lifecycle.LifecycleEventObserver { _, event ->
            if (event == androidx.lifecycle.Lifecycle.Event.ON_RESUME) granted = shizukuGranted()
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        val listener = Shizuku.OnRequestPermissionResultListener { _, _ -> granted = shizukuGranted() }
        runCatching { Shizuku.addRequestPermissionResultListener(listener) }
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(observer)
            runCatching { Shizuku.removeRequestPermissionResultListener(listener) }
        }
    }
    LaunchedEffect(show) {
        repeat(10) {
            granted = shizukuGranted()
            kotlinx.coroutines.delay(300)
        }
    }
    val rows = executorRows(context, granted)
    val shizukuUp = shizukuAvailable()

    PrivilegeDialogComponent(
        state = PrivilegeDialogState(
            page = page,
            rows = rows,
            selectedLabel = if (ExecutorPreference.mode == ExecutorMode.Shizuku) "shizuku" else "app",
        ),
        onDismiss = onDismiss,
        onSelect = { label ->
            when (label) {
                "app" -> ExecutorPreference.set(context, ExecutorMode.App)
                "shizuku" -> when {
                    granted -> ExecutorPreference.set(context, ExecutorMode.Shizuku)
                    !shizukuUp -> page = PrivilegeDialogPage.SHIZUKU_MISSING
                    !requestShizuku() -> page = PrivilegeDialogPage.SHIZUKU_MISSING
                }
            }
        },
        onBack = { page = PrivilegeDialogPage.OVERVIEW },
        miuix = miuix,
    )
}
