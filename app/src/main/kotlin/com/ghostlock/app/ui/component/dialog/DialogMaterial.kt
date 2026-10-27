package com.ghostlock.app.ui.component.dialog

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.material3.LoadingIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.MutableState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import com.ghostlock.app.ui.component.markdown.MarkdownContent
import com.ghostlock.app.ui.component.material.ExpressiveDialog

@Composable
fun LoadingDialogMaterial(
    showDialog: MutableState<Boolean>,
) {
    if (showDialog.value) {
        Dialog(
            onDismissRequest = { },
            // Keep the dialog non-dismissible
            properties = DialogProperties(
                dismissOnClickOutside = false,
                dismissOnBackPress = false,
            )
        ) {
            Surface(
                modifier = Modifier.size(100.dp), shape = MaterialTheme.shapes.extraLarge
            ) {
                Box(
                    contentAlignment = Alignment.Center,
                ) {
                    LoadingIndicator()
                }
            }
        }
    }
}

@Composable
fun ConfirmDialogMaterial(
    visuals: ConfirmDialogVisuals,
    confirm: () -> Unit,
    dismiss: () -> Unit,
    showDialog: MutableState<Boolean>
) {
    if (showDialog.value) {
        ExpressiveDialog(
            onDismissRequest = {
                dismiss()
                showDialog.value = false
            },
            title = { Text(visuals.title) },
            text = visuals.content?.let { content ->
                {
                    when {
                        // Registry content (and any HTML, which only comes from the registry) is rendered locked down.
                    visuals.isRestricted || visuals.isHtml -> MarkdownContent(
                        content = content,
                        isMarkdown = !visuals.isHtml,
                    )
                    visuals.isMarkdown -> MarkdownContent(content = content, isMarkdown = true)
                    else -> DialogMessageBody(text = content, miuix = false)
                    }
                }
            },
            confirmButton = {
                DialogTextButton(
                    action = DialogAction(
                        text = visuals.confirm ?: stringResource(id = android.R.string.ok),
                        onClick = {
                            confirm()
                            showDialog.value = false
                        },
                    ),
                    miuix = false,
                )
            },
            dismissButton = {
                DialogTextButton(
                    action = DialogAction(
                        text = visuals.dismiss ?: stringResource(id = android.R.string.cancel),
                        onClick = {
                            dismiss()
                            showDialog.value = false
                        },
                    ),
                    miuix = false,
                )
            }
        )
    }
}

/**
 * 自定义内容对话框（Material 壳）：复用 [ExpressiveDialog] 的视觉规范（形状/容器色/标题样式），
 * 供需要复杂内容的对话框使用，避免各页面内联 BasicAlertDialog + Surface 自拼样式。
 *
 * @param onDismissRequest 点外部/返回的关闭回调。
 * @param title 标题文案（由调用方本地化）。
 * @param confirmButton 底部确认区（不需要时传空 lambda）。
 * @param content 对话框主体内容。
 * @note 阶段：task-28（对话框组件化）；内容样式由调用方负责，壳不复制 ConfirmDialog 私有实现。
 */
@Composable
fun ContentDialogMaterial(
    onDismissRequest: () -> Unit,
    title: String,
    confirmButton: @Composable () -> Unit = {},
    content: @Composable () -> Unit,
) {
    ExpressiveDialog(
        onDismissRequest = onDismissRequest,
        title = { Text(title) },
        text = { content() },
        confirmButton = confirmButton,
    )
}
