package com.ghostlock.app.ui.screen.home

import android.content.pm.PackageManager
import androidx.compose.foundation.clickable
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.ghostlock.app.R
import rikka.shizuku.Shizuku
import top.yukonga.miuix.kmp.basic.Button as MiuixButton
import top.yukonga.miuix.kmp.basic.Text as MiuixText
import top.yukonga.miuix.kmp.basic.TextButton as MiuixTextButton
import top.yukonga.miuix.kmp.window.WindowDialog

/**
 *  权限状态（2026-10-18 接线）。
 *
 *  主页那一行显示当前执行体（app/shizuku/adb）与 uid；点箭头进子菜单：
 *   · 解释三种执行体各自的权限边界
 *   · 向 Shizuku 请求授权（shell uid 2000）
 *   · 给出 adb 授权/验证步骤（adb shell 同样是 uid 2000）
 */
enum class ExecutorMode { App, Shizuku }

/** 当前选用的执行体（用户可在子菜单里切回 app）。 */
object ExecutorPreference {
    private const val PREFS = "ghostlock.privilege"
    private const val KEY = "executor_mode"

    var mode by mutableStateOf(ExecutorMode.App)

    fun load(context: android.content.Context) {
        mode = if (context.getSharedPreferences(PREFS, 0).getString(KEY, "app") == "shizuku") {
            ExecutorMode.Shizuku
        } else {
            ExecutorMode.App
        }
    }

    fun set(context: android.content.Context, value: ExecutorMode) {
        mode = value
        context.getSharedPreferences(PREFS, 0).edit()
            .putString(KEY, if (value == ExecutorMode.Shizuku) "shizuku" else "app")
            .apply()
    }
}

data class ExecutorRow(val label: String, val uid: Int, val state: String, val desc: String, val available: Boolean)

@Composable
private fun executorRows(granted: Boolean): List<ExecutorRow> {
    val context = LocalContext.current
    val appUid = context.applicationInfo.uid
    val shizukuState = when {
        !runCatching { Shizuku.pingBinder() }.getOrDefault(false) ->
            context.getString(R.string.privilege_state_unavailable)
        granted -> context.getString(R.string.privilege_state_granted)
        else -> context.getString(R.string.privilege_state_denied)
    }
    return listOf(
        ExecutorRow("app", appUid, stringResource(R.string.privilege_state_available), stringResource(R.string.privilege_desc_app), true),
        ExecutorRow("shizuku", 2000, shizukuState, stringResource(R.string.privilege_desc_shizuku), granted),
        ExecutorRow("adb", 2000, stringResource(R.string.privilege_state_adb_hint), stringResource(R.string.privilege_desc_adb), false),
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

@Composable
private fun PrivilegeBody(
    rows: List<ExecutorRow>,
    currentMode: ExecutorMode,
    miuix: Boolean,
    onSelect: (String) -> Unit,
) {
    Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
        rows.forEach { row ->
            val selected = when (row.label) {
                "app" -> currentMode == ExecutorMode.App
                "shizuku" -> currentMode == ExecutorMode.Shizuku
                else -> false
            }
            if (miuix) {
                val scheme = top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme
                top.yukonga.miuix.kmp.basic.Card(
                    modifier = Modifier
                        .fillMaxWidth()
                        .then(if (row.available) Modifier else Modifier.alpha(0.45f)),
                    colors = top.yukonga.miuix.kmp.basic.CardDefaults.defaultColors(
                        color = when {
                            !row.available -> scheme.surfaceVariant
                            selected -> scheme.primaryContainer
                            else -> scheme.surfaceVariant
                        },
                    ),
                ) {
                    top.yukonga.miuix.kmp.basic.BasicComponent(
                        title = row.label + " · uid " + row.uid,
                        summary = row.state + " · " + row.desc,
                        /* 灰色只是视觉状态：未授权时点击要能触发授权/教程（2026-10-18 用户反馈）。 */
                        enabled = true,
                        onClick = { onSelect(row.label) },
                        endActions = {
                            if (selected) {
                                Icon(Icons.Filled.Check, contentDescription = null)
                            }
                        },
                    )
                }
            } else {
                androidx.compose.material3.Card(
                    modifier = Modifier
                        .fillMaxWidth()
                        .then(if (row.available) Modifier else Modifier.alpha(0.45f)),
                    onClick = { onSelect(row.label) },
                    enabled = true,
                    colors = androidx.compose.material3.CardDefaults.cardColors(
                        containerColor = if (selected) {
                            MaterialTheme.colorScheme.primaryContainer
                        } else {
                            MaterialTheme.colorScheme.surfaceVariant
                        },
                    ),
                ) {
                    androidx.compose.material3.ListItem(
                        headlineContent = { Text(row.label + " · uid " + row.uid) },
                        supportingContent = { Text(row.state + " · " + row.desc) },
                        trailingContent = if (selected) {
                            { Icon(Icons.Filled.Check, contentDescription = null) }
                        } else {
                            null
                        },
                    )
                }
            }
        }
        Text(
            text = stringResource(R.string.privilege_select_hint),
            style = MaterialTheme.typography.labelSmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
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

private enum class PrivilegeMode { Overview, AdbSteps, ShizukuMissing }

@Composable
private fun PrivilegeBackButton(miuix: Boolean, onBack: () -> Unit) {
    if (miuix) {
        MiuixTextButton(text = stringResource(R.string.privilege_back), onClick = onBack)
    } else {
        TextButton(onClick = onBack) { Text(stringResource(R.string.privilege_back)) }
    }
}

/** 权限状态子菜单；[miuix] 为 true 时用 miuix 的 WindowDialog。 */
@Composable
fun PrivilegeDialog(show: Boolean, onDismiss: () -> Unit, miuix: Boolean) {
    if (!show) return
    var mode by remember(show) { mutableStateOf(PrivilegeMode.Overview) }
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
    val rows = executorRows(granted)
    val shizukuAvailable = runCatching { Shizuku.pingBinder() }.getOrDefault(false)

    val body: @Composable () -> Unit = {
        when (mode) {
            PrivilegeMode.Overview -> PrivilegeBody(rows = rows, currentMode = ExecutorPreference.mode, miuix = miuix) { label ->
                when (label) {
                    "app" -> ExecutorPreference.set(context, ExecutorMode.App)
                    "shizuku" -> when {
                        granted -> ExecutorPreference.set(context, ExecutorMode.Shizuku)
                        !shizukuAvailable -> mode = PrivilegeMode.ShizukuMissing
                        !requestShizuku() -> mode = PrivilegeMode.ShizukuMissing
                    }
                    /* adb 未接线：点卡片直接看授权步骤（2026-10-18 用户裁定）。 */
                    "adb" -> mode = PrivilegeMode.AdbSteps
                }
            }
            PrivilegeMode.AdbSteps -> Column {
                Text(
                    text = stringResource(R.string.privilege_adb_steps_full),
                    style = MaterialTheme.typography.bodySmall,
                )
                PrivilegeBackButton(miuix) { mode = PrivilegeMode.Overview }
            }
            PrivilegeMode.ShizukuMissing -> Column {
                Text(
                    text = stringResource(R.string.privilege_shizuku_missing),
                    style = MaterialTheme.typography.bodySmall,
                )
                PrivilegeBackButton(miuix) { mode = PrivilegeMode.Overview }
            }
        }
    }

    if (miuix) {
        WindowDialog(
            show = true,
            title = when (mode) {
                PrivilegeMode.AdbSteps -> stringResource(R.string.privilege_adb_title)
                PrivilegeMode.ShizukuMissing -> "Shizuku"
                else -> stringResource(R.string.home_privilege_status)
            },
            onDismissRequest = onDismiss,
            content = {
                Column(modifier = Modifier.padding(horizontal = 4.dp)) {
                    body()
                }
            },
        )
    } else {
        androidx.compose.material3.BasicAlertDialog(onDismissRequest = onDismiss) {
            androidx.compose.material3.Surface(
                shape = MaterialTheme.shapes.extraLarge,
                tonalElevation = 6.dp,
                color = MaterialTheme.colorScheme.surfaceContainerHigh,
            ) {
                Column(modifier = Modifier.padding(24.dp)) {
                    Text(
                        text = when (mode) {
                            PrivilegeMode.AdbSteps -> stringResource(R.string.privilege_adb_title)
                            PrivilegeMode.ShizukuMissing -> "Shizuku"
                            else -> stringResource(R.string.home_privilege_status)
                        },
                        style = MaterialTheme.typography.headlineSmall,
                    )
                    Column(modifier = Modifier.padding(top = 16.dp)) { body() }
                }
            }
        }
    }
}
