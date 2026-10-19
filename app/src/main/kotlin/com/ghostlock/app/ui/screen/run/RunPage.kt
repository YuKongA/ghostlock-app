package com.ghostlock.app.ui.screen.run

import android.content.Context
import android.content.Intent
import androidx.compose.foundation.background
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.core.content.FileProvider
import java.io.File
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.add
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.systemBars
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.IosShare
import androidx.compose.material.icons.filled.HourglassEmpty
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material3.ExtendedFloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LargeFlexibleTopAppBar
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.rememberTopAppBarState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.ghostlock.app.attack.AttackClient
import com.ghostlock.app.attack.AttackSettings
import com.ghostlock.app.log.LogLevel
import com.ghostlock.app.log.LogOrigin
import com.ghostlock.app.log.LogSettings
import com.ghostlock.app.log.elapsedText
import com.ghostlock.app.ui.component.dialog.rememberConfirmDialog
import com.ghostlock.app.log.originLabel
import com.ghostlock.app.log.log
import com.ghostlock.app.ui.component.material.ExpressiveScaffold
import com.ghostlock.app.ui.component.material.expressiveTopAppBarColors
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.withContext
import kotlinx.coroutines.launch
import androidx.compose.material3.BasicAlertDialog
import androidx.compose.material3.Surface
import androidx.compose.material3.TextButton
import top.yukonga.miuix.kmp.window.WindowDialog
import top.yukonga.miuix.kmp.basic.TextButton as MiuixTextButton
import top.yukonga.miuix.kmp.basic.Button as MiuixButton
import top.yukonga.miuix.kmp.basic.MiuixScrollBehavior
import top.yukonga.miuix.kmp.basic.Scaffold as MiuixScaffold
import top.yukonga.miuix.kmp.basic.Text as MiuixText
import top.yukonga.miuix.kmp.basic.TopAppBar as MiuixTopAppBar
import top.yukonga.miuix.kmp.theme.MiuixTheme

/**
 *  「执行」页（2026-10-18 用户裁定）：
 *  · 页面本身不滚动，只有终端内部滚动；
 *  · 终端填满「执行」标题与底栏之间的区域；
 *  · 右下角按钮三态：开始执行（▶）→ 执行中（⏸ 紧急中止）→ 已完成（✓）；
 *  · 按钮与终端同一 12dp 圆角；miuix 下用同款深色终端卡 + 圆角按钮。
 */
private val TerminalBg = Color(0xFF101418)
private val TerminalFg = Color(0xFF9BE7A0)
private val TerminalDim = Color(0xFF5F7A64)
private val Corner = 12.dp

private enum class RunState { Idle, Running, Aborting, Aborted, Done, RerunReady }

/** 终端的一行：级别决定括号颜色；正文放在第二个 Text 里，换行自动悬挂缩进。 */
private data class TerminalEntry(
    val origin: LogOrigin,
    val level: com.ghostlock.app.log.LogLevel,
    val message: String,
    val elapsedMs: Long = 0L,
)

private fun levelColor(level: com.ghostlock.app.log.LogLevel): Color = when (level) {
    com.ghostlock.app.log.LogLevel.Error -> Color(0xFFFF5252)
    com.ghostlock.app.log.LogLevel.Warn -> Color(0xFFFFD740)
    com.ghostlock.app.log.LogLevel.Info -> Color(0xFF9BE7A0)
    com.ghostlock.app.log.LogLevel.Debug -> Color(0xFFFFFFFF)
    com.ghostlock.app.log.LogLevel.Verbose -> Color(0xFF9E9E9E)
}

private fun stateIcon(state: RunState) = when (state) {
    RunState.Idle -> Icons.Filled.PlayArrow
    RunState.Running -> Icons.Filled.Pause
    RunState.Aborting -> Icons.Filled.HourglassEmpty
    RunState.Aborted -> Icons.Filled.Check
    RunState.Done -> Icons.Filled.Check
    RunState.RerunReady -> Icons.Filled.Refresh
}

private fun runLabel(state: RunState): String = when (state) {
    RunState.Idle -> "开始执行"
    RunState.Running -> "紧急中止"
    RunState.Aborting -> "正在中止"
    RunState.Aborted -> "中止完成"
    RunState.Done -> "已完成"
    RunState.RerunReady -> "重新执行"
}



@Composable
fun RunPageMaterial(bottomInnerPadding: Dp) {
    var state by remember { mutableStateOf(RunState.Idle) }
    var runStartedAt by remember { mutableLongStateOf(0L) }
    val entries = remember {
        mutableStateListOf(
            TerminalEntry(LogOrigin.KOTLIN, LogLevel.Info, "ghostlock terminal", 0L),
            TerminalEntry(LogOrigin.KOTLIN, LogLevel.Info, "waiting for execution", 0L),
        )
    }
    val channel = remember { Channel<TerminalEntry>(Channel.UNLIMITED) }
    LaunchedEffect(Unit) { for (entry in channel) entries += entry }
    val scroll = rememberScrollState()
    val scope = rememberCoroutineScope()
    val context = LocalContext.current
    fun startAttack() {
        entries.clear()
        runStartedAt = android.os.SystemClock.elapsedRealtime()
        state = RunState.Running
        AttackClient.start(
            context.applicationContext,
            onEvent = { origin, level, line ->
                channel.trySend(TerminalEntry(origin, level, line, if (runStartedAt == 0L) 0L else android.os.SystemClock.elapsedRealtime() - runStartedAt))
            },
            onDone = { state = RunState.Done },
        )
    }
    val confirmDialog = rememberConfirmDialog(onConfirm = { startAttack() })
    val scrollBehavior = TopAppBarDefaults.exitUntilCollapsedScrollBehavior(rememberTopAppBarState())

    LaunchedEffect(entries.size) { scroll.animateScrollTo(scroll.maxValue) }

    ExpressiveScaffold(
        topBar = {
            LargeFlexibleTopAppBar(
                title = { Text("执行") },
                colors = expressiveTopAppBarColors(),
                windowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
                scrollBehavior = scrollBehavior,
                actions = {
                    IconButton(onClick = { entries.clear() }) {
                        Icon(Icons.Filled.Delete, contentDescription = "清除日志")
                    }
                    IconButton(onClick = { shareTerminal(context, entries) }) {
                        Icon(Icons.Filled.IosShare, contentDescription = "导出终端")
                    }
                },
            )
        },
        contentWindowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
    ) { innerPadding ->
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .padding(horizontal = 16.dp)
                .padding(top = 12.dp, bottom = bottomInnerPadding + 12.dp)
                .clip(RoundedCornerShape(Corner))
                .background(TerminalBg),
        ) {
            SelectionContainer {
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .verticalScroll(scroll)
                        .padding(12.dp)
                        .padding(bottom = 72.dp),
                    verticalArrangement = Arrangement.spacedBy(2.dp),
                ) {
                    entries.forEach { entry ->
                        Row(modifier = Modifier.fillMaxWidth()) {
                            Text(
                                text = (if (LogSettings.showTimestamp) elapsedText(entry.elapsedMs) + " " else "") + originLabel(entry.origin, entry.level),
                                color = levelColor(entry.level),
                                fontFamily = LogSettings.fontFamily(),
                                style = MaterialTheme.typography.bodySmall,
                            )
                            Text(
                                text = entry.message,
                                modifier = Modifier.weight(1f),
                                color = levelColor(entry.level),
                                fontFamily = LogSettings.fontFamily(),
                                style = MaterialTheme.typography.bodySmall,
                            )
                        }
                    }
                }
            }
            ExtendedFloatingActionButton(
                modifier = Modifier.align(Alignment.BottomEnd).padding(12.dp),
                shape = RoundedCornerShape(Corner),
                onClick = {
                    when (state) {
                        RunState.Idle -> {
                            startAttack()
                        }
                        RunState.Running -> {
                            val abortStarted = android.os.SystemClock.elapsedRealtime()
                            state = RunState.Aborting
                            AttackClient.abort(context.applicationContext) {
                                val remaining = 700L - (android.os.SystemClock.elapsedRealtime() - abortStarted)
                                scope.launch {
                                    kotlinx.coroutines.delay(if (remaining > 0L) remaining else 0L)
                                    state = RunState.Aborted
                                }
                            }
                        }
                        RunState.Aborting -> Unit
                        RunState.Aborted, RunState.Done -> state = RunState.RerunReady
                        RunState.RerunReady -> {
                            if (AttackSettings.warnOnRerun) {
                                confirmDialog.showConfirm(
                                    title = "重新执行？",
                                    content = "将再次发起攻击，确认继续。",
                                    confirm = "确认",
                                    dismiss = "取消",
                                )
                            } else {
                                startAttack()
                            }
                        }
                    }
                    scope.launch { scroll.animateScrollTo(scroll.maxValue) }
                },
                icon = {
                    Icon(
                        imageVector = stateIcon(state),
                        contentDescription = null,
                    )
                },
                text = {
                    Text(runLabel(state))
                },
            )
        }
    }
}

@Composable
fun RunPageMiuix(bottomInnerPadding: Dp) {
    var state by remember { mutableStateOf(RunState.Idle) }
    var runStartedAt by remember { mutableLongStateOf(0L) }
    val entries = remember {
        mutableStateListOf(
            TerminalEntry(LogOrigin.KOTLIN, LogLevel.Info, "ghostlock terminal", 0L),
            TerminalEntry(LogOrigin.KOTLIN, LogLevel.Info, "waiting for execution", 0L),
        )
    }
    val channel = remember { Channel<TerminalEntry>(Channel.UNLIMITED) }
    LaunchedEffect(Unit) { for (entry in channel) entries += entry }
    val scroll = rememberScrollState()
    val scope = rememberCoroutineScope()
    val context = LocalContext.current
    fun startAttack() {
        entries.clear()
        runStartedAt = android.os.SystemClock.elapsedRealtime()
        state = RunState.Running
        AttackClient.start(
            context.applicationContext,
            onEvent = { origin, level, line ->
                channel.trySend(TerminalEntry(origin, level, line, if (runStartedAt == 0L) 0L else android.os.SystemClock.elapsedRealtime() - runStartedAt))
            },
            onDone = { state = RunState.Done },
        )
    }
    val confirmDialog = rememberConfirmDialog(onConfirm = { startAttack() })
    val scrollBehavior = MiuixScrollBehavior()

    LaunchedEffect(entries.size) { scroll.animateScrollTo(scroll.maxValue) }

    MiuixScaffold(
        topBar = {
            MiuixTopAppBar(
                title = "执行",
                scrollBehavior = scrollBehavior,
                actions = {
                    val context = LocalContext.current
                    IconButton(onClick = { entries.clear() }) {
                        Icon(Icons.Filled.Delete, contentDescription = "清除日志")
                    }
                    IconButton(onClick = { shareTerminal(context, entries) }) {
                        Icon(Icons.Filled.IosShare, contentDescription = "导出终端")
                    }
                },
            )
        },
        contentWindowInsets = WindowInsets.systemBars.add(WindowInsets.displayCutout).only(WindowInsetsSides.Horizontal),
    ) { innerPadding ->
        Box(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .padding(horizontal = 16.dp)
                .padding(top = 12.dp, bottom = bottomInnerPadding + 12.dp)
                .clip(RoundedCornerShape(Corner))
                .background(TerminalBg),
        ) {
            SelectionContainer {
                Column(
                    modifier = Modifier
                        .fillMaxSize()
                        .verticalScroll(scroll)
                        .padding(12.dp)
                        .padding(bottom = 72.dp),
                    verticalArrangement = Arrangement.spacedBy(2.dp),
                ) {
                    entries.forEach { entry ->
                        Row(modifier = Modifier.fillMaxWidth()) {
                            MiuixText(
                                text = (if (LogSettings.showTimestamp) elapsedText(entry.elapsedMs) + " " else "") + originLabel(entry.origin, entry.level),
                                color = levelColor(entry.level),
                                fontFamily = LogSettings.fontFamily(),
                                style = MiuixTheme.textStyles.footnote1,
                            )
                            MiuixText(
                                text = entry.message,
                                modifier = Modifier.weight(1f),
                                color = levelColor(entry.level),
                                fontFamily = LogSettings.fontFamily(),
                                style = MiuixTheme.textStyles.footnote1,
                            )
                        }
                    }
                }
            }
            MiuixButton(
                modifier = Modifier
                    .align(Alignment.BottomEnd)
                    .padding(12.dp)
                    .clip(RoundedCornerShape(Corner)),
                onClick = {
                    when (state) {
                        RunState.Idle -> {
                            startAttack()
                        }
                        RunState.Running -> {
                            val abortStarted = android.os.SystemClock.elapsedRealtime()
                            state = RunState.Aborting
                            AttackClient.abort(context.applicationContext) {
                                val remaining = 700L - (android.os.SystemClock.elapsedRealtime() - abortStarted)
                                scope.launch {
                                    kotlinx.coroutines.delay(if (remaining > 0L) remaining else 0L)
                                    state = RunState.Aborted
                                }
                            }
                        }
                        RunState.Aborting -> Unit
                        RunState.Aborted, RunState.Done -> state = RunState.RerunReady
                        RunState.RerunReady -> {
                            if (AttackSettings.warnOnRerun) {
                                confirmDialog.showConfirm(
                                    title = "重新执行？",
                                    content = "将再次发起攻击，确认继续。",
                                    confirm = "确认",
                                    dismiss = "取消",
                                )
                            } else {
                                startAttack()
                            }
                        }
                    }
                },
            ) {
                MiuixText(
                    text = when (state) {
                        RunState.Idle -> "▶  " + runLabel(state)
                        RunState.Running -> "⏸  " + runLabel(state)
                        RunState.Aborting -> "…  " + runLabel(state)
                        RunState.Aborted -> "✓  " + runLabel(state)
                        RunState.Done -> "✓  " + runLabel(state)
                        RunState.RerunReady -> "↻  " + runLabel(state)
                    },
                    style = MiuixTheme.textStyles.button,
                )
            }
        }
    }
}


/** 把终端文本写成 .log 文件，再交给 Android 分享菜单。 */
private fun shareTerminal(context: Context, entries: List<TerminalEntry>) {
    val dir = File(context.cacheDir, "logs").apply { mkdirs() }
    val file = File(dir, "ghostlock-terminal-" + System.currentTimeMillis() + ".log")
    file.writeText(entries.joinToString("\n") { log.format(it.level, it.message) } + "\n")
    val uri = FileProvider.getUriForFile(context, context.packageName + ".ghostlock.fileprovider", file)
    val send = Intent(Intent.ACTION_SEND).apply {
        type = "text/x-log"
        putExtra(Intent.EXTRA_STREAM, uri)
        putExtra(Intent.EXTRA_TITLE, file.name)
        addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
    }
    context.startActivity(Intent.createChooser(send, "导出终端日志"))
}
