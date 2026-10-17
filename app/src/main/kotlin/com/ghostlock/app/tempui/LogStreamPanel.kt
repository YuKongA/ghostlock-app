package com.ghostlock.app.tempui

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.BasicText
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.sp
import com.ghostlock.app.log.LogLine
import com.ghostlock.app.log.UnifiedLog
import com.ghostlock.app.ui.kit.GhostlockKit
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive

/** 屏幕默认显示多少行（用户裁定：默认最后 200 行）。 */
private const val SHOWN_LINES = 200

/** 跟随的采样周期：日志写入是即时的，300 ms 只是屏幕刷新节奏。 */
private const val REFRESH_MS = 300L

/** 等宽、小号、可读（行内容可能很长，所以字号取小、不折行、横向滚动）。 */
private val LOG_TEXT = TextStyle(fontFamily = FontFamily.Monospace, fontSize = 10.sp)

/**
 * 统一日志流面板：**屏幕上的正经日志**。
 *
 * 用户裁定（2026-10-16）：「为什么临时 ui 内的日志和 adb 的不一样，我需要在屏幕上看到正经的日志」。
 * 所以这里显示的不是另一个日志，而是 [UnifiedLog] 交出来的**同一个流**：Kotlin 行（GhostLog）与
 * native 行（gl_log.hpp 写同一个 fd）逐行带着时间戳、来源（K/N）、等级、正文；native 行带模块与行号。
 * 面板读回来的每一行与 `run-as com.ghostlock.app tail files/ghostlock-run.log` 里的那一行逐字相同。
 *
 * 三个控件（最小化，不引 Material 主题改造）：清屏（内存环 + 文件一起清）、复制全部、跟随开关。
 * 列表用等宽字、横向滚动容纳超长行；手指拉离底部时跟随自动关掉，拉回底部再恢复。
 *
 * @param kit       界面套件（按钮从它取，两套控件下行为一致）。
 * @param modifier  由调用方给尺寸（面板在临时界面里占满剩余高度）。
 */
@Composable
fun LogStreamPanel(kit: GhostlockKit, modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val shown = remember { mutableStateListOf<LogLine>() }
    val listState = rememberLazyListState()
    val rowScroll = rememberScrollState()
    var follow by remember { mutableStateOf(true) }
    var status by remember { mutableStateOf("日志: 统一流尚未启动") }
    /* 两种套件下的主题都可能是深色或浅色，所以正文色自己定（等宽日志不做主题化）。 */
    val ink = if (isSystemInDarkTheme()) Color(0xFFE8EAED) else Color(0xFF202124)

    LaunchedEffect(Unit) {
        while (isActive) {
            val fresh = runCatching { UnifiedLog.lines(SHOWN_LINES) }.getOrElse { emptyList<LogLine>() }
            shown.clear()
            shown.addAll(fresh)
            /* 手指把列表拉离底部 ⇒ 跟随自动关掉；拉回底部再开（跟随是「不抢用户的滚动」）。 */
            if (listState.isScrollInProgress && shown.isNotEmpty()) {
                val last = listState.layoutInfo.visibleItemsInfo.lastOrNull()?.index ?: -1
                follow = last >= shown.lastIndex - 1
            }
            status = "已收 " + UnifiedLog.lineCount + " 行 · 文件 " + readableSize(UnifiedLog.bytes) +
                " · 跟随: " + (if (follow) "开" else "关") + " · " + UnifiedLog.registration
            if (follow && shown.isNotEmpty()) {
                listState.scrollToItem(shown.lastIndex)
            }
            delay(REFRESH_MS)
        }
    }

    Column(modifier = modifier) {
        BasicText(text = status, style = LOG_TEXT.copy(color = ink))
        kit.Actions {
            kit.Action("清屏") {
                UnifiedLog.clear()
                shown.clear()
            }
            kit.Action("复制全部") { copyToClipboard(context, shown) }
            kit.Action(if (follow) "跟随: 开" else "跟随: 关") { follow = !follow }
        }
        /* 横向滚动包住列表：超长行不折行也能看全（等宽字下对齐更容易读）。 */
        Row(modifier = Modifier.weight(1f).horizontalScroll(rowScroll)) {
            LazyColumn(state = listState, modifier = Modifier.fillMaxHeight()) {
                items(shown) { line ->
                    BasicText(text = line.render(), style = LOG_TEXT.copy(color = ink))
                }
            }
        }
    }
}

/** 复制当前显示的**全部**行（与屏幕逐字相同；不是文件里被挤出窗口的那部分）。 */
private fun copyToClipboard(context: Context, lines: List<LogLine>) {
    val text = lines.joinToString("\n") { line -> line.render() }
    val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager ?: return
    clipboard.setPrimaryClip(ClipData.newPlainText("ghostlock-run.log", text))
}

/** 文件大小的可读形式（状态行用；超过 1 KB 就按 KB 显示一位小数）。 */
private fun readableSize(bytes: Long): String =
    if (bytes < 1024L) bytes.toString() + " B" else String.format(java.util.Locale.US, "%.1f KB", bytes / 1024.0)
