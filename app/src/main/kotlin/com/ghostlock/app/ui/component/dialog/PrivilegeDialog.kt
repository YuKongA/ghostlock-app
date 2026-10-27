package com.ghostlock.app.ui.component.dialog

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.ghostlock.app.R
import com.ghostlock.app.ui.LocalUiMode
import com.ghostlock.app.ui.UiMode
import top.yukonga.miuix.kmp.basic.BasicComponent
import top.yukonga.miuix.kmp.basic.Card as MiuixCard
import top.yukonga.miuix.kmp.basic.CardDefaults as MiuixCardDefaults
import top.yukonga.miuix.kmp.basic.Text as MiuixText
import top.yukonga.miuix.kmp.theme.MiuixTheme

/**
 * 一个执行体的可用性事实（**不带文案**；文案由组件内本地化）。
 *
 * @note 阶段：task-28；调用方只提供事实，组件负责全部 UI 文案/样式。
 */
enum class PrivilegeAvailability {
    /** 应用自身 uid（总是可用）。 */
    APP,

    /** Shizuku 已授权（shell uid 2000 可用）。 */
    GRANTED,

    /** Shizuku 已安装但未授权。 */
    DENIED,

    /** Shizuku 未安装/未启动（binder 不可 ping）。 */
    UNAVAILABLE,
}

/**
 * 执行体列表的一行（纯 UI 数据，文案由组件按 [availability]/[label] 本地化）。
 *
 * @property label 执行体标识："app" 或 "shizuku"。
 * @property uid 执行体 uid（app=应用 uid，shizuku=2000）。
 * @property availability 可用性事实。
 * @property available 是否可选中（仅视觉灰化依据；未授权时仍可点以触发授权）。
 * @note 阶段：task-28。
 */
@Immutable
data class PrivilegeExecutorRow(
    val label: String,
    val uid: Int,
    val availability: PrivilegeAvailability,
    val available: Boolean,
)

/** 权限提示框当前页面。 */
enum class PrivilegeDialogPage {
    /** 执行体总览（默认）。 */
    OVERVIEW,

    /** Shizuku 缺失/未启动说明页。 */
    SHIZUKU_MISSING,
}

/**
 * 权限提示框的只读 UI 状态（由调用方持有逻辑状态后传入）。
 *
 * @property page 当前页面。
 * @property rows 执行体行（事实）。
 * @property selectedLabel 当前选中的执行体（"app"/"shizuku"）。
 * @note 阶段：task-28。
 */
@Immutable
data class PrivilegeDialogState(
    val page: PrivilegeDialogPage,
    val rows: List<PrivilegeExecutorRow>,
    val selectedLabel: String,
)

/**
 * Shizuku/执行体权限提示框（共享组件统一入口）。
 *
 * 行为与 2026-10-18 接线版完全一致：
 * - Overview：执行体列表（选中打勾）、未授权灰化但仍可点击、底部说明；
 * - ShizukuMissing：说明文案 + Back 返回总览；
 * - 标题：Overview = 主页权限状态文案；ShizukuMissing = "Shizuku"。
 *
 * @param state 只读 UI 状态（页面/行/选中）。
 * @param onDismiss 关闭回调。
 * @param onSelect 选中执行体（"app"/"shizuku"）；授权流程由调用方处理。
 * @param onBack 从 ShizukuMissing 返回 Overview。
 * @param miuix true=Miuix 壳，false=Material 壳；默认按 [LocalUiMode] 分发。
 * @note 阶段：task-28；本组件不持有 Shizuku 权限逻辑，只渲染与转发回调。
 */
@Composable
fun PrivilegeDialog(
    state: PrivilegeDialogState,
    onDismiss: () -> Unit,
    onSelect: (String) -> Unit,
    onBack: () -> Unit,
    miuix: Boolean = LocalUiMode.current == UiMode.Miuix,
) {
    if (miuix) {
        PrivilegeDialogMiuix(state, onDismiss, onSelect, onBack)
    } else {
        PrivilegeDialogMaterial(state, onDismiss, onSelect, onBack)
    }
}

/**
 * Material 变体：使用共享 [ContentDialogMaterial]（复用 ExpressiveDialog 视觉规范）。
 *
 * @param state 只读 UI 状态。
 * @param onDismiss 关闭回调。
 * @param onSelect 选中执行体回调。
 * @param onBack 返回总览回调。
 * @note 阶段：task-28。
 */
@Composable
fun PrivilegeDialogMaterial(
    state: PrivilegeDialogState,
    onDismiss: () -> Unit,
    onSelect: (String) -> Unit,
    onBack: () -> Unit,
) {
    ContentDialogMaterial(
        onDismissRequest = onDismiss,
        title = privilegeDialogTitle(state.page),
        confirmButton = {},
    ) {
        /* task-31：不再额外加内边距；正文/动作内边距与 ConfirmDialogMaterial 一致（AlertDialog 正文区）。 */
        PrivilegeDialogContent(state, onSelect, onBack, miuix = false)
    }
}

/**
 * Miuix 变体：使用共享 [ContentDialogMiuix]（复用 WindowDialog 视觉规范）。
 *
 * @param state 只读 UI 状态。
 * @param onDismiss 关闭回调。
 * @param onSelect 选中执行体回调。
 * @param onBack 返回总览回调。
 * @note 阶段：task-28。
 */
@Composable
fun PrivilegeDialogMiuix(
    state: PrivilegeDialogState,
    onDismiss: () -> Unit,
    onSelect: (String) -> Unit,
    onBack: () -> Unit,
) {
    ContentDialogMiuix(
        title = privilegeDialogTitle(state.page),
        onDismissRequest = onDismiss,
    ) {
        /* task-31：不再额外加内边距；与 ConfirmDialogMiuix 的 WindowDialog 内容区规范一致。 */
        PrivilegeDialogContent(state, onSelect, onBack, miuix = true)
    }
}

/**
 * @return 提示框标题（ShizukuMissing 固定 "Shizuku"，其余用主页权限状态文案）。
 */
@Composable
private fun privilegeDialogTitle(page: PrivilegeDialogPage): String = when (page) {
    PrivilegeDialogPage.SHIZUKU_MISSING -> "Shizuku"
    PrivilegeDialogPage.OVERVIEW -> stringResource(R.string.home_privilege_status)
}

/**
 * 提示框主体：总览（执行体列表 + 说明）或 Shizuku 缺失说明 + Back。
 *
 * @param state 只读 UI 状态。
 * @param onSelect 选中执行体回调。
 * @param onBack 返回总览回调。
 * @param miuix 是否 Miuix 风格（决定列表/说明/按钮样式）。
 */
@Composable
private fun PrivilegeDialogContent(
    state: PrivilegeDialogState,
    onSelect: (String) -> Unit,
    onBack: () -> Unit,
    miuix: Boolean,
) {
    when (state.page) {
        PrivilegeDialogPage.OVERVIEW -> PrivilegeOverviewBody(state, onSelect, miuix)
        PrivilegeDialogPage.SHIZUKU_MISSING -> Column {
            /* task-30/31：正文走共享 DialogMessageBody（ConfirmDialog 同源 typography/行距）。
             * `\n\n` 折叠为段落间距、1/2/3 步骤按编号列悬挂缩进呈现；文案资源语义不变。 */
            DialogMessageBody(
                text = stringResource(R.string.privilege_shizuku_missing),
                miuix = miuix,
            )
            /* task-30/31：动作走共享 DialogActionRow（与 ConfirmDialog 动作区同规范：
             * Material 底部右侧 TextButton；Miuix 底部动作行按 weight 铺排，非浮动小块）。 */
            DialogActionRow(
                actions = listOf(
                    DialogAction(
                        text = stringResource(R.string.privilege_back),
                        onClick = onBack,
                    ),
                ),
                miuix = miuix,
            )
        }
    }
}

/**
 * 执行体总览列表（两变体共用同一数据，样式按 [miuix] 分支）。
 *
 * @param state 只读 UI 状态。
 * @param onSelect 选中执行体回调。
 * @param miuix 是否 Miuix 风格。
 */
@Composable
private fun PrivilegeOverviewBody(
    state: PrivilegeDialogState,
    onSelect: (String) -> Unit,
    miuix: Boolean,
) {
    Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
        state.rows.forEach { row ->
            val selected = row.label == state.selectedLabel
            if (miuix) {
                val scheme = MiuixTheme.colorScheme
                MiuixCard(
                    modifier = Modifier
                        .fillMaxWidth()
                        .then(if (row.available) Modifier else Modifier.alpha(0.45f)),
                    colors = MiuixCardDefaults.defaultColors(
                        color = when {
                            !row.available -> scheme.surfaceVariant
                            selected -> scheme.primaryContainer
                            else -> scheme.surfaceVariant
                        },
                    ),
                ) {
                    BasicComponent(
                        title = row.label + " · uid " + row.uid,
                        summary = privilegeRowStateText(row) + " · " + privilegeRowDesc(row),
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
                Card(
                    modifier = Modifier
                        .fillMaxWidth()
                        .then(if (row.available) Modifier else Modifier.alpha(0.45f)),
                    onClick = { onSelect(row.label) },
                    enabled = true,
                    colors = CardDefaults.cardColors(
                        containerColor = if (selected) {
                            MaterialTheme.colorScheme.primaryContainer
                        } else {
                            MaterialTheme.colorScheme.surfaceVariant
                        },
                    ),
                ) {
                    ListItem(
                        supportingContent = {
                            Text(privilegeRowStateText(row) + " · " + privilegeRowDesc(row))
                        },
                        trailingContent = if (selected) {
                            { Icon(Icons.Filled.Check, contentDescription = null) }
                        } else {
                            null
                        },
                    ) {
                        Text(row.label + " · uid " + row.uid)
                    }
                }
            }
        }
        /* Miuix 模式没有 MaterialTheme：颜色必须取 Miuix 的色板，否则暗色下是深灰字。 */
        if (miuix) {
            MiuixText(
                text = stringResource(R.string.privilege_select_hint),
                style = MiuixTheme.textStyles.footnote2,
                color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
            )
        } else {
            Text(
                text = stringResource(R.string.privilege_select_hint),
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

/**
 * @return 该执行体的本地化状态文案。
 */
@Composable
private fun privilegeRowStateText(row: PrivilegeExecutorRow): String = when (row.availability) {
    PrivilegeAvailability.APP -> stringResource(R.string.privilege_state_available)
    PrivilegeAvailability.GRANTED -> stringResource(R.string.privilege_state_granted)
    PrivilegeAvailability.DENIED -> stringResource(R.string.privilege_state_denied)
    PrivilegeAvailability.UNAVAILABLE -> stringResource(R.string.privilege_state_unavailable)
}

/**
 * @return 该执行体的本地化权限边界说明。
 */
@Composable
private fun privilegeRowDesc(row: PrivilegeExecutorRow): String =
    if (row.label == "app") {
        stringResource(R.string.privilege_desc_app)
    } else {
        stringResource(R.string.privilege_desc_shizuku)
    }
