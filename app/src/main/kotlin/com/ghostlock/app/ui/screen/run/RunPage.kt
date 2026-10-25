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
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.ghostlock.app.R
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
import kotlinx.coroutines.flow.collect
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

/**
 * 执行页文案（2026-10-19 本地化）：两套 UI 共用一份，资源只在 Composable 里读。
 *
 * @property title 顶栏标题。
 * @property clearLog 顶栏「清除日志」的无障碍描述。
 * @property shareLog 顶栏「导出终端」的无障碍描述。
 * @property shareChooser 系统分享菜单的标题。
 * @property rerunTitle 二次执行确认框标题。
 * @property rerunMessage 二次执行确认框正文。
 * @property confirm 确认按钮。
 * @property dismiss 取消按钮。
 */
private data class RunLabels(
    val title: String,
    val clearLog: String,
    val shareLog: String,
    val shareChooser: String,
    val rerunTitle: String,
    val rerunMessage: String,
    val confirm: String,
    val dismiss: String,
)

/** 读一次执行页资源；onClick 闭包里不再调用 stringResource。 */
@Composable
private fun runLabels(): RunLabels = RunLabels(
    title = stringResource(R.string.run_page_title),
    clearLog = stringResource(R.string.run_action_clear_log),
    shareLog = stringResource(R.string.run_action_share_log),
    shareChooser = stringResource(R.string.run_share_chooser_title),
    rerunTitle = stringResource(R.string.run_rerun_title),
    rerunMessage = stringResource(R.string.run_rerun_message),
    confirm = stringResource(R.string.confirm),
    dismiss = stringResource(android.R.string.cancel),
)

/** 按钮文案随状态走（资源见 values/strings.xml 与 values-zh-rCN/strings.xml）。 */
@Composable
private fun runLabel(state: RunState): String = stringResource(
    when (state) {
        RunState.Idle -> R.string.run_state_start
        RunState.Running -> R.string.run_state_abort
        RunState.Aborting -> R.string.run_state_aborting
        RunState.Aborted -> R.string.run_state_aborted
        RunState.Done -> R.string.run_state_done
        RunState.RerunReady -> R.string.run_state_rerun
    },
)



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
        channel.trySend(
            TerminalEntry(
                LogOrigin.KOTLIN,
                LogLevel.Warn,
                context.getString(R.string.run_backend_not_wired),
                0L,
            ),
        )
        state = RunState.Done
    }
    val confirmDialog = rememberConfirmDialog(onConfirm = { startAttack() })
    val scrollBehavior = TopAppBarDefaults.exitUntilCollapsedScrollBehavior(rememberTopAppBarState())
    val labels = runLabels()

    /* 终端跟随：内容高度一变就贴底。snapshotFlow 在布局后拿到新的 maxValue，
       用即时 scrollTo 而不是 animateScrollTo —— 后者会被下一行打断成每几百毫秒一跳（2026-10-19）。 */
    LaunchedEffect(scroll) {
        snapshotFlow { scroll.maxValue }.collect { target -> scroll.scrollTo(target) }
    }

    ExpressiveScaffold(
        topBar = {
            LargeFlexibleTopAppBar(
                title = { Text(labels.title) },
                colors = expressiveTopAppBarColors(),
                windowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
                scrollBehavior = scrollBehavior,
                actions = {
                    IconButton(onClick = { entries.clear() }) {
                        Icon(Icons.Filled.Delete, contentDescription = labels.clearLog)
                    }
                    IconButton(onClick = { shareTerminal(context, entries, labels.shareChooser) }) {
                        Icon(Icons.Filled.IosShare, contentDescription = labels.shareLog)
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
                            state = RunState.Aborted
                        }
                        RunState.Aborting -> Unit
                        RunState.Aborted, RunState.Done -> state = RunState.RerunReady
                        RunState.RerunReady -> {
                            if (AttackSettings.warnOnRerun) {
                                confirmDialog.showConfirm(
                                    title = labels.rerunTitle,
                                    content = labels.rerunMessage,
                                    confirm = labels.confirm,
                                    dismiss = labels.dismiss,
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
        channel.trySend(
            TerminalEntry(
                LogOrigin.KOTLIN,
                LogLevel.Warn,
                context.getString(R.string.run_backend_not_wired),
                0L,
            ),
        )
        state = RunState.Done
    }
    val confirmDialog = rememberConfirmDialog(onConfirm = { startAttack() })
    val scrollBehavior = MiuixScrollBehavior()
    val labels = runLabels()

    /* 终端跟随：内容高度一变就贴底。snapshotFlow 在布局后拿到新的 maxValue，
       用即时 scrollTo 而不是 animateScrollTo —— 后者会被下一行打断成每几百毫秒一跳（2026-10-19）。 */
    LaunchedEffect(scroll) {
        snapshotFlow { scroll.maxValue }.collect { target -> scroll.scrollTo(target) }
    }

    MiuixScaffold(
        topBar = {
            MiuixTopAppBar(
                title = labels.title,
                scrollBehavior = scrollBehavior,
                actions = {
                    val context = LocalContext.current
                    IconButton(onClick = { entries.clear() }) {
                        /* Miuix 的 TopAppBar 不给 actions 注入 LocalContentColor，暗色下 Icon 默认取黑色；
                         * 显式给 onBackground（与 AboutMiuix / FinishUi 同口径，2026-10-19）。 */
                        Icon(
                            Icons.Filled.Delete,
                            contentDescription = labels.clearLog,
                            tint = MiuixTheme.colorScheme.onBackground,
                        )
                    }
                    IconButton(onClick = { shareTerminal(context, entries, labels.shareChooser) }) {
                        Icon(
                            Icons.Filled.IosShare,
                            contentDescription = labels.shareLog,
                            tint = MiuixTheme.colorScheme.onBackground,
                        )
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
                            state = RunState.Aborted
                        }
                        RunState.Aborting -> Unit
                        RunState.Aborted, RunState.Done -> state = RunState.RerunReady
                        RunState.RerunReady -> {
                            if (AttackSettings.warnOnRerun) {
                                confirmDialog.showConfirm(
                                    title = labels.rerunTitle,
                                    content = labels.rerunMessage,
                                    confirm = labels.confirm,
                                    dismiss = labels.dismiss,
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
private fun shareTerminal(context: Context, entries: List<TerminalEntry>, chooserTitle: String) {
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
    context.startActivity(Intent.createChooser(send, chooserTitle))
}
