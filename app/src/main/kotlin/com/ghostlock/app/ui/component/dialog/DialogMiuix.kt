package com.ghostlock.app.ui.component.dialog

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.systemBars
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.runtime.Composable
import androidx.compose.runtime.MutableState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.Layout
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.navigationevent.NavigationEventInfo
import androidx.navigationevent.compose.NavigationBackHandler
import androidx.navigationevent.compose.rememberNavigationEventState
import com.ghostlock.app.R
import com.ghostlock.app.ui.component.markdown.MarkdownContent
import top.yukonga.miuix.kmp.basic.InfiniteProgressIndicator
import top.yukonga.miuix.kmp.basic.Text
import top.yukonga.miuix.kmp.theme.LocalDismissState
import top.yukonga.miuix.kmp.theme.MiuixTheme
import top.yukonga.miuix.kmp.window.WindowDialog

@Composable
fun LoadingDialogMiuix(
    showDialog: MutableState<Boolean>,
) {
    WindowDialog(
        show = showDialog.value,
        content = {
            // Consume the back gesture before the dialog's own handler
            val navEventState = rememberNavigationEventState(NavigationEventInfo.None)
            NavigationBackHandler(
                state = navEventState,
                isBackEnabled = true,
                onBackCompleted = { },
            )
            Box(
                modifier = Modifier.fillMaxWidth(),
                contentAlignment = Alignment.CenterStart
            ) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.Start,
                ) {
                    InfiniteProgressIndicator(
                        color = MiuixTheme.colorScheme.onBackground
                    )
                    Text(
                        modifier = Modifier.padding(start = 12.dp),
                        text = stringResource(R.string.processing),
                        fontWeight = FontWeight.Medium
                    )
                }
            }
        }
    )
}

@Composable
fun ConfirmDialogMiuix(
    visuals: ConfirmDialogVisuals,
    confirm: () -> Unit,
    dismiss: () -> Unit,
    showDialog: MutableState<Boolean>
) {
    WindowDialog(
        show = showDialog.value,
        modifier = Modifier.windowInsetsPadding(WindowInsets.systemBars.only(WindowInsetsSides.Top)),
        title = visuals.title,
        onDismissRequest = {
            dismiss()
            showDialog.value = false
        },
        content = {
            Layout(
                content = {
                    val dismissState = LocalDismissState.current
                    visuals.content?.let { content ->
                        when {
                            // Registry content (and any HTML, which only comes from the registry) is rendered locked down.
                        visuals.isRestricted || visuals.isHtml -> MarkdownContent(
                            content = content,
                            isMarkdown = !visuals.isHtml,
                        )
                        visuals.isMarkdown -> MarkdownContent(content = content, isMarkdown = true)
                        else -> DialogMessageBody(text = content, miuix = true)
                        }
                    }
                    DialogActionRow(
                        actions = listOf(
                            DialogAction(
                                text = visuals.dismiss ?: stringResource(id = android.R.string.cancel),
                                onClick = {
                                    dismiss()
                                    dismissState?.invoke()
                                },
                            ),
                            DialogAction(
                                text = visuals.confirm ?: stringResource(id = android.R.string.ok),
                                primary = true,
                                onClick = {
                                    confirm()
                                    dismissState?.invoke()
                                },
                            ),
                        ),
                        miuix = true,
                    )
                }
            ) { measurables, constraints ->
                if (measurables.size != 2) {
                    val button = measurables[0].measure(constraints)
                    layout(constraints.maxWidth, button.height) {
                        button.place(0, 0)
                    }
                } else {
                    val button = measurables[1].measure(constraints)
                    val lazyList = measurables[0].measure(constraints.copy(maxHeight = constraints.maxHeight - button.height))
                    layout(constraints.maxWidth, lazyList.height + button.height) {
                        lazyList.place(0, 0)
                        button.place(0, lazyList.height)
                    }
                }
            }
        }
    )
}

/**
 * 自定义内容对话框（Miuix 壳）：复用 [WindowDialog] 与 ConfirmDialogMiuix 相同的窗口/标题/返回规范，
 * 内容由调用方提供，避免页面内联 WindowDialog 自拼。
 *
 * @param title 标题文案（由调用方本地化）。
 * @param onDismissRequest 关闭回调。
 * @param content 对话框主体内容。
 * @note 阶段：task-28（对话框组件化）。
 */
@Composable
fun ContentDialogMiuix(
    title: String,
    onDismissRequest: () -> Unit,
    content: @Composable () -> Unit,
) {
    WindowDialog(
        show = true,
        modifier = Modifier.windowInsetsPadding(WindowInsets.systemBars.only(WindowInsetsSides.Top)),
        title = title,
        onDismissRequest = onDismissRequest,
        content = { content() },
    )
}
