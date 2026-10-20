package com.ghostlock.app.ui.screen.module.backends

import android.content.Context
import android.content.Intent
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.animateContentSize
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.ui.draw.shadow
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.add
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.systemBars
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.automirrored.filled.Sort
import androidx.compose.material.icons.filled.AdminPanelSettings
import androidx.compose.material.icons.filled.Apps
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.DragHandle
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.Extension
import androidx.compose.material.icons.filled.FilterList
import androidx.compose.material.icons.filled.HelpOutline
import androidx.compose.material.icons.filled.RestartAlt
import androidx.compose.material.icons.filled.Security
import androidx.compose.material.icons.filled.Terminal
import androidx.compose.material3.CheckableDropdownMenuItem
import androidx.compose.material3.DropdownMenuGroup
import androidx.compose.material3.DropdownMenuPopup
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LargeFlexibleTopAppBar
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.MenuDefaults
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SelectableDropdownMenuItem
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.rememberTopAppBarState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.zIndex
import com.ghostlock.app.R
import com.ghostlock.app.ui.PageChrome
import com.ghostlock.app.ui.component.AppIconImage
import com.ghostlock.app.ui.component.material.ExpressiveScaffold
import com.ghostlock.app.ui.component.material.SegmentedColumn
import com.ghostlock.app.ui.component.material.SegmentedListItem
import com.ghostlock.app.ui.component.material.TonalCard
import com.ghostlock.app.ui.component.material.expressiveTopAppBarColors
import top.yukonga.miuix.kmp.basic.MiuixScrollBehavior
import top.yukonga.miuix.kmp.basic.Scaffold as MiuixScaffold
import top.yukonga.miuix.kmp.basic.TextField as MiuixTextField
import top.yukonga.miuix.kmp.basic.TopAppBar as MiuixTopAppBar
import top.yukonga.miuix.kmp.preference.ArrowPreference
import top.yukonga.miuix.kmp.preference.RadioButtonLocation
import top.yukonga.miuix.kmp.preference.RadioButtonPreference
import top.yukonga.miuix.kmp.preference.SwitchPreference
import kotlin.math.roundToInt

/* ---------------- payload metadata ---------------- */

/** 载荷类型对应的标题文案。 */
@Composable
private fun payloadTitle(kind: PayloadKind): String = stringResource(
    when (kind) {
        PayloadKind.Root -> R.string.finish_root_manager
        PayloadKind.Command -> R.string.finish_script
        PayloadKind.Lkm -> R.string.finish_lkm
    },
)

/** 载荷类型对应的图标。 */
private fun payloadIcon(kind: PayloadKind) = when (kind) {
    PayloadKind.Root -> Icons.Filled.AdminPanelSettings
    PayloadKind.Command -> Icons.Filled.Terminal
    PayloadKind.Lkm -> Icons.Filled.Extension
}

/* ---------------- entry card ---------------- */

@Composable
fun FinishCard(miuix: Boolean, onOpen: () -> Unit) {
    val context = LocalContext.current
    val state = FinishConfig.state
    val enabled = state.payloads.filter { it.enabled }.map { payloadTitle(it.kind) }
    val summary = when (enabled.size) {
        0 -> stringResource(R.string.finish_none)
        1 -> enabled.first()
        else -> stringResource(R.string.finish_multiple)
    }
    if (miuix) {
        top.yukonga.miuix.kmp.basic.Card(modifier = Modifier.fillMaxWidth()) {
            ArrowPreference(
                title = stringResource(R.string.payload_title),
                summary = summary,
                onClick = { FinishConfig.load(context); onOpen() },
                startAction = {
                    Icon(
                        imageVector = Icons.Filled.CheckCircle,
                        contentDescription = null,
                        tint = top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme.onBackground,
                    )
                },
            )
        }
    } else {
        TonalCard(modifier = Modifier.fillMaxWidth()) {
            SegmentedColumn(Modifier.fillMaxWidth()) {
                item {
                    SegmentedListItem(
                        onClick = { FinishConfig.load(context); onOpen() },
                        leadingContent = { Icon(Icons.Filled.CheckCircle, contentDescription = null) },
                        headlineContent = { Text(stringResource(R.string.payload_title)) },
                        supportingContent = { Text(summary) },
                    )
                }
            }
        }
    }
}

/* ---------------- page ---------------- */

@Composable
fun FinishPage(miuix: Boolean, bottomInnerPadding: Dp, onBack: () -> Unit) {
    DisposableEffect(Unit) {
        PageChrome.hideBottomBar = true
        onDispose { PageChrome.hideBottomBar = false }
    }
    FinishMain(miuix, bottomInnerPadding, onBack)
}

/* ---------------- main ---------------- */

@Composable
private fun FinishMain(
    miuix: Boolean,
    bottomInnerPadding: Dp,
    onBack: () -> Unit,
) {
    val context = LocalContext.current
    var state by remember { mutableStateOf(FinishConfig.state) }
    /* 2026-10-19 用户裁定：右上角从「排序」改为「编辑」；载荷可重复添加/删除。 */
    var editMode by remember { mutableStateOf(false) }
    /* 编辑模式的底部按钮改成「添加载荷」，点开是四选一弹窗（2026-10-19）。 */
    var showAdd by remember { mutableStateOf(false) }
    /* 未填完整的启用项：标红，且普通模式禁止保存脚本（2026-10-19 用户裁定）。 */
    val hasIncomplete = state.payloads.any { it.enabled && !it.isComplete() }
    fun commit(next: FinishState) {
        state = next
        FinishConfig.update(context, next)
    }
    fun updateSlot(slot: PayloadSlot) {
        commit(state.copy(payloads = state.payloads.map { if (it.id == slot.id) slot else it }))
    }
    fun deleteSlot(id: String) {
        commit(state.copy(payloads = state.payloads.filterNot { it.id == id }))
    }
    fun addSlot(kind: PayloadKind) {
        commit(state.copy(payloads = state.payloads + newPayloadSlot(kind)))
    }

    if (miuix) {
        val scrollBehavior = MiuixScrollBehavior()
        MiuixScaffold(
            topBar = {
                MiuixTopAppBar(
                    title = stringResource(R.string.payload_title),
                    navigationIcon = { IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, null, tint = top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme.onBackground) } },
                    actions = {
                        IconButton(onClick = { editMode = !editMode }) {
                            Icon(
                                imageVector = if (editMode) Icons.Filled.Check else Icons.Filled.Edit,
                                contentDescription = stringResource(if (editMode) R.string.payload_edit_done else R.string.payload_edit),
                                tint = top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme.onBackground,
                            )
                        }
                    },
                    scrollBehavior = scrollBehavior,
                )
            },
            /* 覆盖层页面没有外层底栏：底部导航条留白必须自己吃，否则固定按钮被三大金刚键挡住（2026-10-19）。 */
            contentWindowInsets = WindowInsets.systemBars.add(WindowInsets.displayCutout)
                .only(WindowInsetsSides.Horizontal + WindowInsetsSides.Bottom),
        ) { inner ->
            val pageScroll = rememberScrollState()
            Column(modifier = Modifier.fillMaxSize().padding(inner).imePadding()) {
                Column(
                    modifier = Modifier.weight(1f).fillMaxWidth()
                        .verticalScroll(pageScroll)
                        .padding(horizontal = 16.dp, vertical = 12.dp)
                        .padding(bottom = bottomInnerPadding),
                    verticalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    if (editMode) {
                        /* 编辑模式只做三件事：加、排序、删；参数区不出现（2026-10-19 用户裁定）。 */
                        if (state.payloads.isNotEmpty()) {
                            PayloadEditList(
                                miuix = true,
                                slots = state.payloads,
                                rootManager = state.rootManager,
                                onDelete = ::deleteSlot,
                                onMove = { next -> commit(state.copy(payloads = next)) },
                            )
                        }
                    } else {
                        if (state.payloads.isEmpty()) PayloadEmptyCard(miuix = true)
                        state.payloads.forEach { slot ->
                            PayloadSlotCard(
                                miuix = true,
                                slot = slot,
                                incomplete = slot.enabled && !slot.isComplete(),
                                rootManager = state.rootManager,
                                safeMode = state.safeMode,
                                onChange = ::updateSlot,
                                onRootManager = { name -> commit(state.copy(rootManager = name)) },
                                onSafeMode = { on -> commit(state.copy(safeMode = on)) },
                            )
                        }
                    }
                }
                /* 底部固定按钮：普通模式保存预览；编辑模式打开「添加载荷」弹窗（2026-10-19）。 */
                top.yukonga.miuix.kmp.basic.Button(
                    modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 12.dp),
                    enabled = editMode || !hasIncomplete,
                    onClick = {
                        if (editMode) {
                            showAdd = true
                        } else {
                            val file = FinishScriptStore.preview(context, state)
                            android.widget.Toast.makeText(context, context.getString(R.string.finish_saved, file.absolutePath), android.widget.Toast.LENGTH_LONG).show()
                        }
                    },
                ) {
                    top.yukonga.miuix.kmp.basic.Text(
                        stringResource(if (editMode) R.string.payload_add else R.string.finish_save),
                    )
                }
            }
        }
    } else {
        val contextM = LocalContext.current
        val scrollBehavior = TopAppBarDefaults.exitUntilCollapsedScrollBehavior(rememberTopAppBarState())
        ExpressiveScaffold(
            topBar = {
                LargeFlexibleTopAppBar(
                    title = { Text(stringResource(R.string.payload_title)) },
                    navigationIcon = { IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, null) } },
                    actions = {
                        IconButton(onClick = { editMode = !editMode }) {
                            Icon(
                                imageVector = if (editMode) Icons.Filled.Check else Icons.Filled.Edit,
                                contentDescription = stringResource(if (editMode) R.string.payload_edit_done else R.string.payload_edit),
                            )
                        }
                    },
                    colors = expressiveTopAppBarColors(),
                    windowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
                    scrollBehavior = scrollBehavior,
                )
            },
            /* 覆盖层无底栏：底部只吃系统栏；IME 留白交给内容根的 imePadding()，
               避免 safeDrawing 已含 IME 时二次叠加（2026-10-19 用户裁定）。 */
            contentWindowInsets = WindowInsets.systemBars.add(WindowInsets.displayCutout)
                .only(WindowInsetsSides.Horizontal + WindowInsetsSides.Bottom),
        ) { inner ->
            val pageScroll = rememberScrollState()
            Column(modifier = Modifier.fillMaxSize().padding(inner).imePadding()) {
                Column(
                    modifier = Modifier.weight(1f).fillMaxWidth()
                        .nestedScroll(scrollBehavior.nestedScrollConnection)
                        .verticalScroll(pageScroll)
                        .padding(horizontal = 16.dp).padding(top = 16.dp, bottom = bottomInnerPadding),
                    verticalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    if (editMode) {
                        if (state.payloads.isNotEmpty()) {
                            PayloadEditList(
                                miuix = false,
                                slots = state.payloads,
                                rootManager = state.rootManager,
                                onDelete = ::deleteSlot,
                                onMove = { next -> commit(state.copy(payloads = next)) },
                            )
                        }
                    } else {
                        if (state.payloads.isEmpty()) PayloadEmptyCard(miuix = false)
                        state.payloads.forEach { slot ->
                            PayloadSlotCard(
                                miuix = false,
                                slot = slot,
                                incomplete = slot.enabled && !slot.isComplete(),
                                rootManager = state.rootManager,
                                safeMode = state.safeMode,
                                onChange = ::updateSlot,
                                onRootManager = { name -> commit(state.copy(rootManager = name)) },
                                onSafeMode = { on -> commit(state.copy(safeMode = on)) },
                            )
                        }
                    }
                }
                androidx.compose.material3.Button(
                    modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 12.dp),
                    enabled = editMode || !hasIncomplete,
                    onClick = {
                        if (editMode) {
                            showAdd = true
                        } else {
                            val file = FinishScriptStore.preview(contextM, state)
                            android.widget.Toast.makeText(contextM, contextM.getString(R.string.finish_saved, file.absolutePath), android.widget.Toast.LENGTH_LONG).show()
                        }
                    },
                ) {
                    Text(stringResource(if (editMode) R.string.payload_add else R.string.finish_save))
                }
            }
        }
    }

    AddPayloadDialog(miuix = miuix, show = showAdd, onDismiss = { showAdd = false }, onAdd = ::addSlot)
}

/* ---------------- payload cards ---------------- */

/**
 * 一条载荷实例的卡片（2026-10-19 用户裁定）。
 *
 * 头部是类型名 + 开关；开关打开才渲染参数区。同类实例各自持有一份参数，
 * 所以两张「命令」卡片可以是两条不同命令。编辑模式的列表是另一套行（PayloadEditList）。
 */
@Composable
private fun PayloadSlotCard(
    miuix: Boolean,
    slot: PayloadSlot,
    incomplete: Boolean,
    rootManager: String,
    safeMode: Boolean,
    onChange: (PayloadSlot) -> Unit,
    onRootManager: (String) -> Unit,
    onSafeMode: (Boolean) -> Unit,
) {
    val context = LocalContext.current
    val scriptPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        onChange(slot.copy(scriptUri = uri?.toString()))
    }
    val lkmPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        onChange(slot.copy(lkmUri = uri?.toString()))
    }

    val content: @Composable () -> Unit = {
        /* 底部补 8dp：最后一行到卡片下边框与首行到上边框等距（2026-10-19 用户反馈）。 */
        Column(Modifier.fillMaxWidth().padding(bottom = 8.dp)) {
            PayloadHeader(miuix = miuix, slot = slot, incomplete = incomplete, onChange = onChange)
            /* 开关关掉时只留头部：参数区隐藏，脚本生成也会跳过这条。 */
            if (slot.enabled) {
                when (slot.kind) {
                    PayloadKind.Root -> {
                        FinishConfig.rootManagers.forEach { spec ->
                            val installedPkgs = FinishConfig.installedPackages(context, spec)
                            val installed = installedPkgs.isNotEmpty()
                            PayloadRow(
                                miuix = miuix,
                                title = spec.name,
                                summary = if (installed) installedPkgs.first() else stringResource(R.string.finish_manager_missing, spec.name),
                                enabled = installed,
                                onClick = if (installed) ({ onRootManager(spec.name) }) else null,
                                leading = { ManagerIcon(miuix, spec) },
                                trailing = { PayloadRadio(miuix, selected = rootManager == spec.name, enabled = installed) },
                            )
                        }
                        /* 安全模式只在支持 ksud late-load 的管理器上出现（FolkPatch 是 launchOnly）。 */
                        if (FinishConfig.spec(rootManager)?.launchOnly != true) {
                            PayloadRow(
                                miuix = miuix,
                                title = stringResource(R.string.safe_mode_label),
                                summary = stringResource(R.string.safe_mode_summary),
                                onClick = { onSafeMode(!safeMode) },
                                trailing = {
                                    if (miuix) {
                                        top.yukonga.miuix.kmp.basic.Switch(
                                            checked = safeMode,
                                            onCheckedChange = onSafeMode,
                                        )
                                    } else {
                                        Switch(checked = safeMode, onCheckedChange = onSafeMode)
                                    }
                                },
                            )
                        }
                    }

                    PayloadKind.Command -> {
                        PayloadRow(
                            miuix = miuix,
                            title = stringResource(R.string.finish_mode_command),
                            error = incomplete,
                            onClick = { onChange(slot.copy(scriptMode = 0)) },
                            trailing = { PayloadRadio(miuix, selected = slot.scriptMode == 0) },
                        )
                        PayloadRow(
                            miuix = miuix,
                            title = stringResource(R.string.finish_mode_script),
                            onClick = { onChange(slot.copy(scriptMode = 1)) },
                            trailing = { PayloadRadio(miuix, selected = slot.scriptMode == 1) },
                        )
                        if (slot.scriptMode == 0) {
                            PayloadTextField(
                                miuix = miuix,
                                value = slot.command,
                                onValueChange = { onChange(slot.copy(command = it)) },
                                label = stringResource(R.string.finish_command_hint),
                            )
                        } else {
                            PayloadRow(
                                miuix = miuix,
                                title = stringResource(R.string.finish_script_pick),
                                summary = slot.scriptUri ?: stringResource(R.string.finish_script_none),
                                error = incomplete,
                                onClick = { scriptPicker.launch(arrayOf("text/x-shellscript", "text/plain", "application/octet-stream")) },
                                trailing = { PayloadArrow(miuix) },
                            )
                        }
                    }

                    PayloadKind.Lkm -> {
                        PayloadRow(
                            miuix = miuix,
                            title = stringResource(R.string.finish_lkm_pick),
                            summary = slot.lkmUri ?: stringResource(R.string.finish_lkm_none),
                            error = incomplete,
                            onClick = { lkmPicker.launch(arrayOf("application/octet-stream", "*/*")) },
                            trailing = { PayloadArrow(miuix) },
                        )
                        PayloadTextField(
                            miuix = miuix,
                            value = slot.lkmParams,
                            onValueChange = { onChange(slot.copy(lkmParams = it)) },
                            label = stringResource(R.string.finish_lkm_params),
                        )
                    }
                }
            }
        }
    }

    if (miuix) {
        top.yukonga.miuix.kmp.basic.Card(Modifier.fillMaxWidth().animateContentSize()) { content() }
    } else {
        TonalCard { content() }
    }
}

/** leading 槽固定 36dp：图标大小不同也不会推动文字起点（2026-10-19 用户裁定）。 */
private val PayloadIconSlot = 36.dp

/** 槽与文字之间的固定间距。 */
private val PayloadGap = 12.dp

/** 固定宽度 leading 槽。 */
@Composable
private fun PayloadLeadingSlot(content: @Composable () -> Unit) {
    Box(Modifier.size(PayloadIconSlot), contentAlignment = Alignment.Center) { content() }
}

/**
 * 载荷卡片的统一行：槽 36dp + 间距 12dp + 标题/摘要 + 尾部控件。
 *
 * 头部与所有参数行共用它，标题与详情文字左对齐（2026-10-19 用户裁定）。
 */
@Composable
private fun PayloadRow(
    miuix: Boolean,
    title: String,
    summary: String? = null,
    enabled: Boolean = true,
    /** true 时标题/摘要用 error 色：表示这一项没填完整（2026-10-19 用户裁定）。 */
    error: Boolean = false,
    onClick: (() -> Unit)? = null,
    leading: @Composable () -> Unit = {},
    trailing: @Composable () -> Unit = {},
) {
    val click = if (onClick != null) Modifier.clickable(enabled = enabled, onClick = onClick) else Modifier
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .heightIn(min = 56.dp)
            .then(click)
            .padding(horizontal = 16.dp, vertical = 8.dp)
            .alpha(if (enabled) 1f else 0.45f),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        PayloadLeadingSlot(leading)
        Spacer(Modifier.width(PayloadGap))
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.Center) {
            if (miuix) {
                top.yukonga.miuix.kmp.basic.Text(
                    text = title,
                    style = top.yukonga.miuix.kmp.theme.MiuixTheme.textStyles.body1,
                    color = if (error) top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme.error else Color.Unspecified,
                )
                summary?.let {
                    top.yukonga.miuix.kmp.basic.Text(
                        text = it,
                        style = top.yukonga.miuix.kmp.theme.MiuixTheme.textStyles.footnote1,
                        color = if (error) {
                            top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme.error
                        } else {
                            top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme.onSurfaceVariantSummary
                        },
                    )
                }
            } else {
                Text(
                    text = title,
                    style = MaterialTheme.typography.bodyLarge,
                    color = if (error) MaterialTheme.colorScheme.error else Color.Unspecified,
                )
                summary?.let {
                    Text(
                        text = it,
                        style = MaterialTheme.typography.bodyMedium,
                        color = if (error) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
        }
        trailing()
    }
}

/** 头部行：类型图标 + 标题 + 开关；普通模式不显示副标题（2026-10-19 用户裁定）。 */
@Composable
private fun PayloadHeader(
    miuix: Boolean,
    slot: PayloadSlot,
    incomplete: Boolean,
    onChange: (PayloadSlot) -> Unit,
) {
    PayloadRow(
        miuix = miuix,
        title = payloadTitle(slot.kind),
        summary = if (incomplete) stringResource(R.string.payload_incomplete) else null,
        error = incomplete,
        onClick = { onChange(slot.copy(enabled = !slot.enabled)) },
        leading = {
            Icon(
                imageVector = payloadIcon(slot.kind),
                contentDescription = null,
                modifier = Modifier.size(24.dp),
                tint = when {
                    incomplete -> if (miuix) {
                        top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme.error
                    } else {
                        MaterialTheme.colorScheme.error
                    }
                    miuix -> top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme.onBackground
                    else -> MaterialTheme.colorScheme.onSurfaceVariant
                },
            )
        },
        trailing = {
            if (miuix) {
                top.yukonga.miuix.kmp.basic.Switch(
                    checked = slot.enabled,
                    onCheckedChange = { onChange(slot.copy(enabled = it)) },
                )
            } else {
                Switch(checked = slot.enabled, onCheckedChange = { onChange(slot.copy(enabled = it)) })
            }
        },
    )
}

/** 行尾单选框。 */
@Composable
private fun PayloadRadio(miuix: Boolean, selected: Boolean, enabled: Boolean = true) {
    if (miuix) {
        top.yukonga.miuix.kmp.basic.RadioButton(selected = selected, onClick = null, enabled = enabled)
    } else {
        RadioButton(selected = selected, onClick = null, enabled = enabled)
    }
}

/** 行尾箭头。 */
@Composable
private fun PayloadArrow(miuix: Boolean) {
    Icon(
        imageVector = Icons.AutoMirrored.Filled.KeyboardArrowRight,
        contentDescription = null,
        tint = if (miuix) {
            top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme.onSurfaceVariantSummary
        } else {
            MaterialTheme.colorScheme.onSurfaceVariant
        },
    )
}

/** 输入框：左缘与行文字对齐（抵掉输入框自带的 16dp 内边距）。 */
@Composable
private fun PayloadTextField(
    miuix: Boolean,
    value: String,
    onValueChange: (String) -> Unit,
    label: String,
) {
    val start = 16.dp + PayloadIconSlot + PayloadGap - 16.dp
    if (miuix) {
        MiuixTextField(
            value = value,
            onValueChange = onValueChange,
            label = label,
            modifier = Modifier.padding(start = start, end = 16.dp, top = 8.dp, bottom = 8.dp),
        )
    } else {
        OutlinedTextField(
            value = value,
            onValueChange = onValueChange,
            label = { Text(label) },
            modifier = Modifier.fillMaxWidth().padding(start = start, end = 16.dp, top = 8.dp, bottom = 8.dp),
            singleLine = true,
        )
    }
}

/**
 * 编辑模式行的小标题（2026-10-19 用户裁定）：
 * root=管理器名、app=应用名、cmd=命令或脚本文件名、lkm=ko 文件名。
 *
 * @param slot 载荷实例。
 * @param rootManager 当前 root 管理器。
 * @return 单行文本；未配置时给占位文案。
 */
@Composable
private fun payloadEditSubtitle(slot: PayloadSlot, rootManager: String): String = when (slot.kind) {
    PayloadKind.Root -> rootManager
    PayloadKind.Command -> if (slot.scriptMode == 0) {
        slot.command.ifBlank { stringResource(R.string.payload_not_configured) }
    } else {
        slot.scriptUri?.substringAfterLast('/')?.takeIf { it.isNotBlank() }
            ?: stringResource(R.string.finish_script_none)
    }
    PayloadKind.Lkm -> slot.lkmUri?.substringAfterLast('/')?.takeIf { it.isNotBlank() }
        ?: stringResource(R.string.finish_lkm_none)
}

/**
 * 编辑模式的载荷行（2026-10-19 用户裁定）：
 * 左边是删除键，中间是载荷名，右边是排序把手；参数区不在编辑模式出现。
 */
@Composable
private fun PayloadEditList(
    miuix: Boolean,
    slots: List<PayloadSlot>,
    rootManager: String,
    onDelete: (String) -> Unit,
    onMove: (List<PayloadSlot>) -> Unit,
) {
    val rowHeight = 72.dp
    val rowPx = with(LocalDensity.current) { rowHeight.toPx() }
    var draggingId by remember { mutableStateOf<String?>(null) }
    var dragOffset by remember { mutableFloatStateOf(0f) }
    val latest by rememberUpdatedState(slots)

    /**
     * 越过半行就交换位置：其它行由 animateItem() 补间，被拖的行跟手（不加动画，避免双重位移）。
     * 松手时已被 onMove 落到目标位置，不需要回弹。
     */
    fun dragBy(slotId: String, amountY: Float) {
        dragOffset += amountY
        var current = latest.indexOfFirst { it.id == slotId }
        if (current < 0) return
        while (dragOffset > rowPx / 2 && current < latest.lastIndex) {
            val next = latest.toMutableList().apply { add(current + 1, removeAt(current)) }
            onMove(next)
            current += 1
            dragOffset -= rowPx
        }
        while (dragOffset < -rowPx / 2 && current > 0) {
            val next = latest.toMutableList().apply { add(current - 1, removeAt(current)) }
            onMove(next)
            current -= 1
            dragOffset += rowPx
        }
    }

    val content: @Composable () -> Unit = {
        LazyColumn(
            modifier = Modifier.fillMaxWidth().height(rowHeight * latest.size),
            userScrollEnabled = false,
        ) {
            itemsIndexed(latest, key = { _, slot -> slot.id }) { _, slot ->
                val dragging = draggingId == slot.id
                val incomplete = slot.enabled && !slot.isComplete()
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(rowHeight)
                        .then(if (dragging) Modifier else Modifier.animateItem())
                        .zIndex(if (dragging) 1f else 0f)
                        .offset {
                            if (dragging) IntOffset(0, dragOffset.roundToInt()) else IntOffset.Zero
                        }
                        .padding(start = 4.dp, end = 4.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    IconButton(onClick = { onDelete(slot.id) }) {
                        Icon(
                            imageVector = Icons.Filled.Delete,
                            contentDescription = stringResource(R.string.payload_delete),
                            tint = if (miuix) {
                                top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme.error
                            } else {
                                MaterialTheme.colorScheme.error
                            },
                        )
                    }
                    Column(Modifier.weight(1f)) {
                        if (miuix) {
                            top.yukonga.miuix.kmp.basic.Text(
                                text = payloadTitle(slot.kind),
                                color = if (incomplete) {
                                    top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme.error
                                } else {
                                    Color.Unspecified
                                },
                            )
                            top.yukonga.miuix.kmp.basic.Text(
                                text = payloadEditSubtitle(slot, rootManager),
                                style = top.yukonga.miuix.kmp.theme.MiuixTheme.textStyles.footnote1,
                                color = top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme.onSurfaceVariantSummary,
                                maxLines = 1,
                                overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis,
                            )
                        } else {
                            Text(
                                text = payloadTitle(slot.kind),
                                color = if (incomplete) MaterialTheme.colorScheme.error else Color.Unspecified,
                            )
                            Text(
                                text = payloadEditSubtitle(slot, rootManager),
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                maxLines = 1,
                                overflow = androidx.compose.ui.text.style.TextOverflow.Ellipsis,
                            )
                        }
                    }
                    PayloadDragHandle(
                        slotId = slot.id,
                        tint = if (miuix) {
                            top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme.onBackground
                        } else {
                            MaterialTheme.colorScheme.onSurfaceVariant
                        },
                        onStart = { draggingId = slot.id; dragOffset = 0f },
                        onDrag = { dragBy(slot.id, it) },
                        onEnd = { draggingId = null; dragOffset = 0f },
                        onCancel = { draggingId = null; dragOffset = 0f },
                    )
                }
            }
        }
    }

    if (miuix) {
        top.yukonga.miuix.kmp.basic.Card(Modifier.fillMaxWidth()) { content() }
    } else {
        TonalCard { content() }
    }
}

/** 排序把手：拖动量交给列表换算目标下标。 */
@Composable
private fun PayloadDragHandle(
    slotId: String,
    tint: androidx.compose.ui.graphics.Color,
    onStart: () -> Unit,
    onDrag: (Float) -> Unit,
    onEnd: () -> Unit,
    onCancel: () -> Unit,
) {
    Icon(
        imageVector = Icons.Filled.DragHandle,
        contentDescription = stringResource(R.string.payload_sort_handle),
        tint = tint,
        modifier = Modifier
            .size(48.dp)
            .pointerInput(slotId) {
                detectDragGestures(
                    onDragStart = { onStart() },
                    onDragEnd = { onEnd() },
                    onDragCancel = { onCancel() },
                    onDrag = { change, amount ->
                        change.consume()
                        onDrag(amount.y)
                    },
                )
            },
    )
}

/** 空载荷表：只提示去编辑模式加一条。 */
@Composable
private fun PayloadEmptyCard(miuix: Boolean) {
    if (miuix) {
        top.yukonga.miuix.kmp.basic.Card(Modifier.fillMaxWidth()) {
            top.yukonga.miuix.kmp.basic.BasicComponent(title = stringResource(R.string.payload_empty))
        }
    } else {
        TonalCard {
            SegmentedColumn(Modifier.fillMaxWidth()) {
                item {
                    SegmentedListItem(headlineContent = { Text(stringResource(R.string.payload_empty)) })
                }
            }
        }
    }
}

/** 编辑模式底部的「添加载荷」弹窗：四类各一行，选中后追加一条并关闭。 */
@Composable
private fun AddPayloadDialog(
    miuix: Boolean,
    show: Boolean,
    onDismiss: () -> Unit,
    onAdd: (PayloadKind) -> Unit,
) {
    if (!show) return
    val kinds = listOf(PayloadKind.Root, PayloadKind.Command, PayloadKind.Lkm)
    val pick: (PayloadKind) -> Unit = { kind ->
        onAdd(kind)
        onDismiss()
    }
    if (miuix) {
        top.yukonga.miuix.kmp.window.WindowDialog(
            show = true,
            title = stringResource(R.string.payload_add),
            onDismissRequest = onDismiss,
            content = {
                Column {
                    kinds.forEach { kind ->
                        top.yukonga.miuix.kmp.basic.BasicComponent(
                            title = payloadTitle(kind),
                            startAction = {
                                Icon(
                                    imageVector = payloadIcon(kind),
                                    contentDescription = null,
                                    modifier = Modifier.padding(end = 6.dp),
                                    tint = top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme.onBackground,
                                )
                            },
                            onClick = { pick(kind) },
                        )
                    }
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
                Column(Modifier.padding(vertical = 20.dp)) {
                    Text(
                        text = stringResource(R.string.payload_add),
                        style = MaterialTheme.typography.titleLarge,
                        modifier = Modifier.padding(horizontal = 24.dp, vertical = 8.dp),
                    )
                    kinds.forEach { kind ->
                        androidx.compose.material3.ListItem(
                            headlineContent = { Text(payloadTitle(kind)) },
                            leadingContent = { Icon(payloadIcon(kind), contentDescription = null) },
                            modifier = Modifier.clickable { pick(kind) },
                        )
                    }
                }
            }
        }
    }
}

/* ---------------- helpers ---------------- */

@Composable
private fun ManagerIcon(miuix: Boolean, spec: FinishConfig.RootManagerSpec) {
    val context = LocalContext.current
    val installed = FinishConfig.installedPackages(context, spec)
    val pkg = installed.firstOrNull() ?: spec.packages.first()
    val info = remember(pkg) { runCatching { context.packageManager.getApplicationInfo(pkg, 0) }.getOrNull() }
    if (info != null) {
        AppIconImage(Modifier.size(36.dp), info, info.loadLabel(context.packageManager).toString())
    } else {
        Box(Modifier.size(36.dp), contentAlignment = Alignment.Center) {
            Icon(
                imageVector = Icons.Filled.HelpOutline,
                contentDescription = null,
                modifier = Modifier.size(20.dp),
                tint = if (miuix) {
                    top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme.onSurfaceVariantSummary
                } else {
                    MaterialTheme.colorScheme.onSurfaceVariant
                },
            )
        }
    }
}
