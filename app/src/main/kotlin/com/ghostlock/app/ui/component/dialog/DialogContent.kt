package com.ghostlock.app.ui.component.dialog

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import com.ghostlock.app.ui.LocalUiMode
import com.ghostlock.app.ui.UiMode
import top.yukonga.miuix.kmp.basic.ButtonDefaults as MiuixButtonDefaults
import top.yukonga.miuix.kmp.basic.Text as MiuixText
import top.yukonga.miuix.kmp.basic.TextButton as MiuixTextButton

/**
 * 对话框的一个动作按钮声明（文案 + 点击 + 是否主按钮）。
 *
 * @property text 已本地化的按钮文案。
 * @property primary 是否主按钮（Miuix 用 primary 文本色；Material 默认同 ConfirmDialog 的 TextButton）。
 * @property onClick 点击回调。
 * @note 阶段：task-30/31；动作组件由 `ui/component/dialog` 统一样式，页面不得自拼按钮。
 */
@Immutable
data class DialogAction(
    val text: String,
    val primary: Boolean = false,
    val onClick: () -> Unit,
)

/**
 * 对话框正文的一类结构化块。
 *
 * @note 阶段：task-31；解析结果仅供 [DialogMessageBody] 渲染，不暴露给页面。
 */
private sealed interface DialogBlock {
    /** 普通段落行。 */
    data class Paragraph(val text: String) : DialogBlock

    /** 编号步骤：编号列固定宽度，正文列悬挂缩进。 */
    data class Step(val marker: String, val text: String) : DialogBlock
}

/**
 * 对话框正文（Material/Miuix 统一 typography、行距、段落间距与编号步骤排布）。
 *
 * 排版规则（抽象自 `ConfirmDialogMaterial` / `ConfirmDialogMiuix` 的纯文本正文样式）：
 * - **折叠连续空行**：`\n\n` 不渲染为大段空白；段落间距固定 10dp，相邻步骤间距 6dp；
 * - **编号步骤结构化**：`1.`/`2.`/... 渲染为 `Row { 编号固定 22dp 列（右对齐）; 正文 Text(weight=1f) }`，
 *   长文本折行悬挂在正文列，编号不参与折行；
 * - **样式同源**：Material `bodyMedium` + `onSurfaceVariant`；Miuix `body1`（ConfirmDialog 正文样式）。
 *
 * @param text 已本地化正文；`\n` 为换行、连续空行折叠，编号行按步骤渲染。
 * @param miuix 是否 Miuix 风格；默认按 [LocalUiMode] 分发。
 * @note 阶段：task-31。
 */
@Composable
fun DialogMessageBody(
    text: String,
    miuix: Boolean = LocalUiMode.current == UiMode.Miuix,
) {
    val blocks = remember(text) { parseDialogBlocks(text) }
    if (blocks.isEmpty()) {
        DialogMessageLine(text = text, miuix = miuix, modifier = Modifier.fillMaxWidth())
        return
    }
    Column(modifier = Modifier.fillMaxWidth()) {
        blocks.forEachIndexed { index, block ->
            if (index > 0) {
                val previous = blocks[index - 1]
                val gap = if (previous is DialogBlock.Step && block is DialogBlock.Step) {
                    STEP_GAP
                } else {
                    PARAGRAPH_GAP
                }
                Spacer(Modifier.height(gap))
            }
            when (block) {
                is DialogBlock.Step -> Row(
                    modifier = Modifier.fillMaxWidth(),
                    verticalAlignment = Alignment.Top,
                ) {
                    DialogMessageLine(
                        text = block.marker,
                        miuix = miuix,
                        modifier = Modifier
                            .width(STEP_MARKER_WIDTH)
                            .padding(end = STEP_MARKER_GAP),
                        textAlign = TextAlign.End,
                    )
                    DialogMessageLine(
                        text = block.text,
                        miuix = miuix,
                        modifier = Modifier.weight(1f),
                    )
                }
                is DialogBlock.Paragraph -> DialogMessageLine(
                    text = block.text,
                    miuix = miuix,
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        }
    }
}

/**
 * 对话框正文的一行（Material `bodyMedium` / Miuix `body1`，与 ConfirmDialog 正文同源）。
 *
 * @param text 已本地化文本。
 * @param miuix 是否 Miuix 风格。
 * @param modifier 布局修饰符。
 * @param textAlign 文本对齐（编号列右对齐）。
 */
@Composable
private fun DialogMessageLine(
    text: String,
    miuix: Boolean,
    modifier: Modifier = Modifier,
    textAlign: TextAlign = TextAlign.Start,
) {
    if (miuix) {
        /* 与 ConfirmDialogMiuix 正文同源：Miuix 默认正文样式（不额外指定 style/color）。 */
        MiuixText(
            text = text,
            modifier = modifier,
            textAlign = textAlign,
        )
    } else {
        Text(
            text = text,
            modifier = modifier,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            textAlign = textAlign,
        )
    }
}

/**
 * 解析正文块：折叠连续空行，识别 `1.`/`2.` 形式的编号步骤。
 *
 * @param text 原始本地化正文。
 * @return 非空块序列；无内容时返回空列表。
 */
private fun parseDialogBlocks(text: String): List<DialogBlock> {
    val lines = text.split('\n').map { it.trim() }.filter { it.isNotEmpty() }
    return lines.map { line ->
        val match = STEP_REGEX.find(line)
        if (match != null) {
            DialogBlock.Step(
                marker = match.groupValues[1],
                text = match.groupValues[2].trim(),
            )
        } else {
            DialogBlock.Paragraph(line)
        }
    }
}

/**
 * 对话框文本按钮（复用 ConfirmDialogMaterial 的 `TextButton` 与 ConfirmDialogMiuix 的
 * `TextButton` / `textButtonColorsPrimary` 样式）。
 *
 * @param action 动作声明。
 * @param miuix 是否 Miuix 风格；默认按 [LocalUiMode] 分发。
 * @param modifier 布局修饰符（如 Miuix 动作行的 weight）。
 * @note 阶段：task-30。
 */
@Composable
fun DialogTextButton(
    action: DialogAction,
    miuix: Boolean = LocalUiMode.current == UiMode.Miuix,
    modifier: Modifier = Modifier,
) {
    if (miuix) {
        MiuixTextButton(
            text = action.text,
            onClick = action.onClick,
            modifier = modifier,
            colors = if (action.primary) {
                MiuixButtonDefaults.textButtonColorsPrimary()
            } else {
                MiuixButtonDefaults.textButtonColors()
            },
        )
    } else {
        TextButton(onClick = action.onClick, modifier = modifier) {
            Text(action.text)
        }
    }
}

/**
 * 对话框动作行（与 `ConfirmDialogMiuix` / `ConfirmDialogMaterial` 的动作区规范一致）。
 *
 * 布局规则（ConfirmDialog 同源，页面禁止自拼）：
 * - **Miuix**：`Row(SpaceBetween, fillMaxWidth, padding(top=12dp))`，按钮按 `weight(1f)` 铺排
 *   （单动作同样占满该行，避免浮动小胶囊——与 ConfirmDialogMiuix 底部动作行一致），
 *   双按钮之间 20dp 间距；
 * - **Material**：`Row(End, fillMaxWidth, padding(top=12dp))`，自然宽度 `TextButton` 右对齐，
 *   与 ConfirmDialogMaterial 的按钮区习惯一致。
 *
 * @param actions 动作列表（Miuix 双按钮顺序=dismiss、confirm）。
 * @param miuix 是否 Miuix 风格；默认按 [LocalUiMode] 分发。
 * @note 阶段：task-30/31。
 */
@Composable
fun DialogActionRow(
    actions: List<DialogAction>,
    miuix: Boolean = LocalUiMode.current == UiMode.Miuix,
) {
    if (actions.isEmpty()) return
    if (miuix) {
        Row(
            horizontalArrangement = Arrangement.SpaceBetween,
            modifier = Modifier
                .fillMaxWidth()
                .padding(top = 12.dp),
        ) {
            if (actions.size == 2) {
                DialogTextButton(action = actions[0], miuix = true, modifier = Modifier.weight(1f))
                Spacer(Modifier.width(20.dp))
                DialogTextButton(action = actions[1], miuix = true, modifier = Modifier.weight(1f))
            } else {
                actions.forEach { action ->
                    DialogTextButton(action = action, miuix = true, modifier = Modifier.weight(1f))
                }
            }
        }
    } else {
        Row(
            horizontalArrangement = Arrangement.End,
            modifier = Modifier
                .fillMaxWidth()
                .padding(top = 12.dp),
        ) {
            actions.forEach { action -> DialogTextButton(action = action, miuix = false) }
        }
    }
}

/** 编号列宽度。 */
private val STEP_MARKER_WIDTH = 22.dp

/** 编号列与正文列间距。 */
private val STEP_MARKER_GAP = 8.dp

/** 编号步骤之间的间距。 */
private val STEP_GAP = 6.dp

/** 段落/段落与步骤组之间的间距。 */
private val PARAGRAPH_GAP = 10.dp

/** 编号行正则：`1.`、`1)`、`1、` 等。 */
private val STEP_REGEX = Regex("^(\\d+[.)、])\\s+(.+)$")
