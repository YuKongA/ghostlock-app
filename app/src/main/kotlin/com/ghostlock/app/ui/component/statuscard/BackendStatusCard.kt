package com.ghostlock.app.ui.component.statuscard

import com.ghostlock.app.ui.icon.Icons as ProjectIcons

import androidx.annotation.StringRes
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.contentColorFor
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ghostlock.app.R
import com.ghostlock.app.ui.LocalUiMode
import com.ghostlock.app.ui.UiMode
import com.ghostlock.app.ui.backend.BackendStatusTone
import com.ghostlock.app.ui.component.statustag.StatusTag
import com.ghostlock.app.ui.theme.isInDarkTheme
import top.yukonga.miuix.kmp.basic.Card as MiuixCard
import top.yukonga.miuix.kmp.basic.CardDefaults as MiuixCardDefaults
import top.yukonga.miuix.kmp.utils.PressFeedbackType
import top.yukonga.miuix.kmp.basic.Text as MiuixText
import top.yukonga.miuix.kmp.theme.MiuixTheme
import top.yukonga.miuix.kmp.theme.MiuixTheme.isDynamicColor

/**
 * 主页 backend 状态卡的共享数据模型。
 *
 * 职责边界（task-23）：
 * - **文本由 backend adapter 提供**：[statusText] 是已解析的完整本地化短句；
 * - **图标/颜色由 UI 框架按 readiness 选择**：[tone] 只表达 READY / NOT_READY；
 * - 本组件只把同一短句放入对应皮肤的标题/原因槽，不解析 reason list、不做 allow/deny 决策。
 *
 * @property cveLabel 当前 backend 的 CVE 编号（右上角 StatusTag，空串不显示）。
 * @property statusText backend adapter 提供的完整本地化状态文案（短句，窄屏最多显示两行）。
 * @property tone READY/NOT_READY，决定图标与容器色。
 * @property navigationTextRes 底部导航行文案（就绪→执行页 / 未就绪→路径页）。
 * @property stateTags 状态标签（安全模式/越狱模式等，warning 语义，复用 StatusTag）。
 * @property extraTags 附注标签（自定义 LKM 等，neutral 语义，复用 StatusTag）。
 * @property onClick 整卡点击。
 * @note 阶段：task-23；组件化共享状态卡，Home Material/Miuix 只调用本组件。
 */
data class BackendStatusCardModel(
    val cveLabel: String,
    val statusText: String,
    val tone: BackendStatusTone,
    @param:StringRes val navigationTextRes: Int,
    val stateTags: List<String> = emptyList(),
    val extraTags: List<String> = emptyList(),
    val onClick: () -> Unit,
)

/**
 * backend 状态卡（按当前 UI 套件分发到 Material / Miuix 实现）。
 *
 * @param model 共享数据模型；文本/状态均来自 backend 声明与 UI 框架映射。
 * @note 内部复用 `ui/component/statustag/StatusTag`；Home 页面只调用本入口。
 */
@Composable
fun BackendStatusCard(model: BackendStatusCardModel) {
    when (LocalUiMode.current) {
        UiMode.Material -> BackendStatusCardMaterial(model)
        UiMode.Miuix -> BackendStatusCardMiuix(model)
    }
}

/**
 * Material 实现：共享状态标题/原因和底部 action/tag 行，保持 Material 容器与小图标外观。
 *
 * @param model 共享数据模型。
 * @return 在当前 composition 中发出一个 Material 状态卡；本函数不执行 I/O。
 * @note NOT_READY 固定显示“Not ready”标题，backend 短句作为原因；导航与 badge 在底部同行，
 * 避免 badge 占用原因文本的测量宽度。
 */
@Composable
internal fun BackendStatusCardMaterial(model: BackendStatusCardModel) {
    val ready = model.tone == BackendStatusTone.READY
    val containerColor = if (ready) {
        MaterialTheme.colorScheme.secondaryContainer
    } else {
        MaterialTheme.colorScheme.errorContainer
    }
    val contentColor = MaterialTheme.colorScheme.contentColorFor(containerColor)
    val accentColor = if (ready) {
        MaterialTheme.colorScheme.primary
    } else {
        MaterialTheme.colorScheme.error
    }
    val statusIcon = if (ready) ProjectIcons.Rounded.CheckCircle.material else ProjectIcons.Rounded.Warning.material

    Surface(
        modifier = Modifier.fillMaxWidth(),
        color = containerColor,
        contentColor = contentColor,
        shape = MaterialTheme.shapes.large,
        onClick = model.onClick,
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(min = 110.dp)
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.SpaceBetween,
        ) {
            Row(
                verticalAlignment = Alignment.Top,
                horizontalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                /* 固定小图标：24dp，位于卡内 leading，不溢出、不遮挡文字。 */
                Icon(
                    imageVector = statusIcon,
                    contentDescription = null,
                    modifier = Modifier.size(24.dp),
                    tint = accentColor,
                )
                Column(
                    modifier = Modifier.weight(1f),
                    verticalArrangement = Arrangement.spacedBy(4.dp),
                ) {
                    Text(
                        text = if (ready) {
                            model.statusText
                        } else {
                            stringResource(R.string.home_not_ready)
                        },
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold,
                        maxLines = 1,
                    )
                    if (!ready) {
                        Text(
                            text = model.statusText,
                            style = MaterialTheme.typography.bodyMedium,
                            maxLines = 2,
                        )
                    }
                }
            }
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(start = 36.dp, top = 8.dp)
                    .clickable { model.onClick() },
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(6.dp),
            ) {
                Text(
                    text = stringResource(model.navigationTextRes),
                    style = MaterialTheme.typography.bodyMedium,
                    fontWeight = FontWeight.Medium,
                )
                Icon(
                    imageVector = ProjectIcons.AutoMirrored.Filled.KeyboardArrowRight.material,
                    contentDescription = null,
                    modifier = Modifier.size(18.dp),
                )
                if (
                    model.cveLabel.isNotEmpty() ||
                    model.stateTags.isNotEmpty() ||
                    model.extraTags.isNotEmpty()
                ) {
                    FlowRow(
                        modifier = Modifier.weight(1f),
                        horizontalArrangement = Arrangement.spacedBy(6.dp, Alignment.End),
                        verticalArrangement = Arrangement.spacedBy(4.dp),
                    ) {
                        if (model.cveLabel.isNotEmpty()) {
                            StatusTag(
                                label = model.cveLabel,
                                contentColor = MaterialTheme.colorScheme.onPrimary,
                                backgroundColor = MaterialTheme.colorScheme.primary,
                            )
                        }
                        for (tag in model.stateTags) {
                            StatusTag(
                                label = tag,
                                contentColor = MaterialTheme.colorScheme.onError,
                                backgroundColor = MaterialTheme.colorScheme.error,
                            )
                        }
                        for (tag in model.extraTags) {
                            StatusTag(
                                label = tag,
                                contentColor = MaterialTheme.colorScheme.onTertiaryContainer,
                                backgroundColor = MaterialTheme.colorScheme.tertiaryContainer,
                            )
                        }
                    }
                }
            }
        }
    }
}

/**
 * 机械移植 GhostLock 1.2 的 `ActivationStatusCard` 几何基底并适配当前两态 backend model。
 *
 * @param model 当前 backend 的状态文本、READY/NOT_READY tone、标签、导航与整卡动作。
 * @return 在当前 composition 中发出一个 Miuix 状态卡；本函数不执行 I/O。
 * @note 直接来源 commit `847793ea7f936c241f38f2ca840d382162ac5256`，
 * `GhostlockUI.kt:739-846`。110 dp 图标故意在 BottomEnd 向右/下偏移并由 Card 裁切，
 * 这正是原 GhostLock 1.2 状态卡基底的视觉特征，而不是 reserved-column 变体。
 * @note 前提：调用方已提供 [MiuixTheme] 与稳定的 [BackendStatusCardModel]；强制明暗模式
 * 继续通过项目 [isInDarkTheme] 解析。
 * @note V0.9b 在不改变共享 model/Material/backend 文本契约的前提下，把 NOT_READY 固定标题与
 * backend 提供的完整状态短句分行；短句保持原样，不解析或拼接“原因”。V0.10d 把 Column 恢复为
 * 普通 16 dp end padding，让先绘制的大图标只作背景装饰、不再预留 120 dp。V0.11 把跳转与
 * CVE/state/extra tags 合并进同一个 full-width action Row：跳转固定 leading，完整 [FlowRow] 使用
 * 剩余宽度并 trailing 对齐；单 badge 不再成为 Column 的独立测量行。V0.12 把标题/原因收进
 * header，并由最小 110 dp 的外层内容列以 SpaceBetween 把完整 action Row 推向卡片底部；内容超过
 * 最小高度时卡片按自然高度增长。失败语义仍为 composition 异常直接上抛。阶段：V0.12b 修订实现。
 */
@Composable
internal fun BackendStatusCardMiuix(model: BackendStatusCardModel) {
    val ready = model.tone == BackendStatusTone.READY
    val colorScheme = MiuixTheme.colorScheme
    val darkTheme = isInDarkTheme()
    val containerColor = when {
        ready && isDynamicColor -> colorScheme.secondaryContainer
        ready && darkTheme -> Color(0xFF1A3825)
        ready -> Color(0xFFDFFAE4)
        isDynamicColor -> colorScheme.errorContainer
        darkTheme -> Color(0xFF3B1D20)
        else -> Color(0xFFFFE8E8)
    }
    val contentColor = colorScheme.onBackground
    val accentColor = when {
        ready && isDynamicColor -> colorScheme.primary.copy(alpha = 0.8f)
        ready -> Color(0xFF36D167)
        isDynamicColor -> colorScheme.error.copy(alpha = 0.8f)
        darkTheme -> Color(0xFFFFB4AB)
        else -> Color(0xFFBA1A1A)
    }
    val statusIcon = if (ready) {
        ProjectIcons.Rounded.CheckCircleOutline.material
    } else {
        ProjectIcons.Rounded.ErrorOutline.material
    }
    val statusText = model.statusText
    MiuixCard(
        modifier = Modifier.fillMaxWidth(),
        colors = MiuixCardDefaults.defaultColors(color = containerColor),
        onClick = model.onClick,
        showIndication = true,
        pressFeedbackType = PressFeedbackType.Tilt,
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(min = 110.dp),
        ) {
            Icon(
                imageVector = statusIcon,
                contentDescription = null,
                tint = accentColor,
                modifier = Modifier
                    .align(Alignment.BottomEnd)
                    .offset(x = 27.dp, y = 31.dp)
                    .size(110.dp),
            )
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .heightIn(min = 110.dp)
                    .padding(
                        start = 16.dp,
                        top = 14.dp,
                        end = 16.dp,
                        bottom = 14.dp
                    ),
                verticalArrangement = Arrangement.SpaceBetween,
            ) {
                Column(
                    modifier = Modifier.padding(bottom = 6.dp),
                    verticalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    MiuixText(
                        text = if (ready) statusText else stringResource(R.string.home_not_ready),
                        color = contentColor,
                        fontSize = 22.sp,
                        fontWeight = FontWeight.SemiBold,
                        maxLines = 1,
                    )
                    if (!ready) {
                        MiuixText(
                            text = statusText,
                            color = contentColor,
                            fontSize = 15.sp,
                            maxLines = 2,
                        )
                    }
                }
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable { model.onClick() }
                        .padding(top = 2.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    MiuixText(
                        text = stringResource(model.navigationTextRes),
                        color = contentColor,
                        fontSize = 15.sp,
                        fontWeight = FontWeight.Medium,
                    )
                    Icon(
                        imageVector = ProjectIcons.AutoMirrored.Filled.KeyboardArrowRight.material,
                        contentDescription = null,
                        modifier = Modifier.size(16.dp),
                        tint = contentColor,
                    )
                    if (
                        model.cveLabel.isNotEmpty() ||
                        model.stateTags.isNotEmpty() ||
                        model.extraTags.isNotEmpty()
                    ) {
                        FlowRow(
                            modifier = Modifier.weight(1f),
                            horizontalArrangement = Arrangement.spacedBy(6.dp, Alignment.End),
                            verticalArrangement = Arrangement.spacedBy(4.dp),
                        ) {
                            if (model.cveLabel.isNotEmpty()) {
                                StatusTag(
                                    label = model.cveLabel,
                                    contentColor = colorScheme.onBackground,
                                    backgroundColor = colorScheme.surface,
                                )
                            }
                            for (tag in model.stateTags) {
                                StatusTag(
                                    label = tag,
                                    contentColor = colorScheme.onBackground,
                                    backgroundColor = colorScheme.surface,
                                )
                            }
                            for (tag in model.extraTags) {
                                StatusTag(
                                    label = tag,
                                    contentColor = colorScheme.onBackground,
                                    backgroundColor = colorScheme.surface,
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}
