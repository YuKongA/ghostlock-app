package com.ghostlock.app.ui.component.statuscard

import androidx.annotation.StringRes
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.rounded.CheckCircle
import androidx.compose.material.icons.rounded.CheckCircleOutline
import androidx.compose.material.icons.rounded.ErrorOutline
import androidx.compose.material.icons.rounded.Warning
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
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ghostlock.app.R
import com.ghostlock.app.ui.LocalUiMode
import com.ghostlock.app.ui.UiMode
import com.ghostlock.app.ui.backend.BackendStatusTone
import com.ghostlock.app.ui.component.statustag.StatusTag
import top.yukonga.miuix.kmp.basic.Card as MiuixCard
import top.yukonga.miuix.kmp.basic.CardDefaults as MiuixCardDefaults
import top.yukonga.miuix.kmp.utils.PressFeedbackType
import top.yukonga.miuix.kmp.basic.Text as MiuixText
import top.yukonga.miuix.kmp.theme.MiuixTheme

/**
 * 主页 backend 状态卡的共享数据模型。
 *
 * 职责边界（task-23）：
 * - **文本由 backend 提供**：[statusTextRes] 是 backend 自己声明的完整本地化短句；
 * - **图标/颜色由 UI 框架按 readiness 选择**：[tone] 只表达 READY / NOT_READY；
 * - 本组件不拼接原因、不显示 detail、不做 allow/deny 决策。
 *
 * @property cveLabel 当前 backend 的 CVE 编号（右上角 StatusTag，空串不显示）。
 * @property statusTextRes backend 提供的完整本地化状态文案（短句，必须整行放得下）。
 * @property tone READY/NOT_READY，决定图标与容器色。
 * @property navigationTextRes 底部导航行文案（就绪→执行页 / 未就绪→路径页）。
 * @property stateTags 状态标签（安全模式/越狱模式等，warning 语义，复用 StatusTag）。
 * @property extraTags 附注标签（自定义 LKM 等，neutral 语义，复用 StatusTag）。
 * @property onClick 整卡点击。
 * @note 阶段：task-23；组件化共享状态卡，Home Material/Miuix 只调用本组件。
 */
data class BackendStatusCardModel(
    val cveLabel: String,
    @param:StringRes val statusTextRes: Int,
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
 * Material 实现：**纯 Row 布局**（leading 图标 + Column + trailing CVE badge），
 * 不使用 ListItem/overlay/绝对定位，避免 badge 压标题、图标溢出卡片。
 *
 * @param model 共享数据模型。
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
    val statusIcon = if (ready) Icons.Rounded.CheckCircle else Icons.Rounded.Warning

    Surface(
        modifier = Modifier.fillMaxWidth(),
        color = containerColor,
        contentColor = contentColor,
        shape = MaterialTheme.shapes.large,
        onClick = model.onClick,
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
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
                verticalArrangement = Arrangement.spacedBy(2.dp),
            ) {
                Text(
                    /* backend 提供的完整短句；单行且不省略（字号已按窄屏收敛）。 */
                    text = stringResource(model.statusTextRes),
                    style = MaterialTheme.typography.titleSmall,
                    maxLines = 1,
                )
                if (model.stateTags.isNotEmpty()) {
                    Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        for (tag in model.stateTags) {
                            StatusTag(
                                label = tag,
                                contentColor = MaterialTheme.colorScheme.onErrorContainer,
                                backgroundColor = MaterialTheme.colorScheme.error,
                            )
                        }
                    }
                }
                Row(
                    modifier = Modifier.clickable { model.onClick() },
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    Text(
                        text = stringResource(model.navigationTextRes),
                        style = MaterialTheme.typography.bodyMedium,
                    )
                    Icon(
                        imageVector = Icons.AutoMirrored.Filled.KeyboardArrowRight,
                        contentDescription = null,
                        modifier = Modifier.size(18.dp),
                    )
                    for (tag in model.extraTags) {
                        StatusTag(
                            label = tag,
                            contentColor = MaterialTheme.colorScheme.onTertiaryContainer,
                            backgroundColor = MaterialTheme.colorScheme.tertiaryContainer,
                        )
                    }
                }
            }
            if (model.cveLabel.isNotEmpty()) {
                /* CVE badge 固定在 trailing，不 overlay。 */
                StatusTag(
                    label = model.cveLabel,
                    contentColor = MaterialTheme.colorScheme.onPrimary,
                    backgroundColor = MaterialTheme.colorScheme.primary,
                )
            }
        }
    }
}

/**
 * Miuix 实现：同样使用**纯 Row 布局**（leading 小图标 + Column + trailing CVE badge），
 * 移除原先的大图标 offset 绝对定位与 CVE 文本叠层，避免溢出/重叠。
 *
 * @param model 共享数据模型。
 */
@Composable
internal fun BackendStatusCardMiuix(model: BackendStatusCardModel) {
    val ready = model.tone == BackendStatusTone.READY
    val colorScheme = MiuixTheme.colorScheme
    val containerColor = if (ready) colorScheme.secondaryContainer else colorScheme.errorContainer
    val accentColor = if (ready) colorScheme.primary else colorScheme.error
    val statusIcon = if (ready) {
        Icons.Rounded.CheckCircleOutline
    } else {
        Icons.Rounded.ErrorOutline
    }
    MiuixCard(
        modifier = Modifier.fillMaxWidth(),
        colors = MiuixCardDefaults.defaultColors(color = containerColor),
        onClick = model.onClick,
        showIndication = true,
        pressFeedbackType = PressFeedbackType.Tilt,
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Icon(
                imageVector = statusIcon,
                contentDescription = null,
                modifier = Modifier.size(24.dp),
                tint = accentColor,
            )
            Column(
                modifier = Modifier.weight(1f),
                verticalArrangement = Arrangement.spacedBy(2.dp),
            ) {
                MiuixText(
                    /* backend 提供的完整短句；单行且不省略。 */
                    text = stringResource(model.statusTextRes),
                    fontSize = 14.sp,
                    maxLines = 1,
                )
                if (model.stateTags.isNotEmpty()) {
                    Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                        for (tag in model.stateTags) {
                            StatusTag(
                                label = tag,
                                contentColor = colorScheme.onBackground,
                                backgroundColor = colorScheme.surface,
                            )
                        }
                    }
                }
                Row(
                    modifier = Modifier.clickable { model.onClick() },
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                ) {
                    MiuixText(
                        text = stringResource(model.navigationTextRes),
                        fontSize = 13.sp,
                    )
                    Icon(
                        imageVector = Icons.AutoMirrored.Filled.KeyboardArrowRight,
                        contentDescription = null,
                        modifier = Modifier.size(16.dp),
                    )
                    for (tag in model.extraTags) {
                        StatusTag(
                            label = tag,
                            contentColor = colorScheme.onBackground,
                            backgroundColor = colorScheme.surface,
                        )
                    }
                }
            }
            if (model.cveLabel.isNotEmpty()) {
                StatusTag(
                    label = model.cveLabel,
                    contentColor = colorScheme.onBackground,
                    backgroundColor = colorScheme.surface,
                )
            }
        }
    }
}
