package com.ghostlock.app.ui.screen.module.backends

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
import androidx.compose.material.icons.filled.DragHandle
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

private enum class FinishRoute { Main, App }

/* ---------------- option metadata ---------------- */

private data class FinishOption(val id: String, val titleRes: Int, val icon: androidx.compose.ui.graphics.vector.ImageVector)

private val payloadIds = listOf("root", "app", "cmd", "lkm")

private val finishOptions = listOf(
    FinishOption("root", R.string.finish_root_manager, Icons.Filled.AdminPanelSettings),
    FinishOption("app", R.string.finish_run_app, Icons.Filled.Apps),
    FinishOption("cmd", R.string.finish_script, Icons.Filled.Terminal),
    FinishOption("selinux", R.string.finish_restore_selinux, Icons.Filled.Security),
    FinishOption("lkm", R.string.finish_lkm, Icons.Filled.Extension),
    FinishOption("reboot", R.string.finish_soft_reboot, Icons.Filled.RestartAlt),
)

/* ---------------- entry card ---------------- */

@Composable
fun FinishCard(miuix: Boolean, onOpen: () -> Unit) {
    val context = LocalContext.current
    val state = FinishConfig.state
    val enabled = buildList {
        if (state.rootManagerEnabled) add(stringResource(R.string.finish_root_manager))
        if (state.runAppEnabled) add(stringResource(R.string.finish_run_app))
        if (state.scriptEnabled) add(stringResource(R.string.finish_script))
        if (state.lkmEnabled) add(stringResource(R.string.finish_lkm))
    }
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
                        modifier = Modifier.padding(end = 6.dp),
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
    var route by remember { mutableStateOf(FinishRoute.Main) }
    DisposableEffect(Unit) {
        PageChrome.hideBottomBar = true
        onDispose { PageChrome.hideBottomBar = false }
    }
    BackHandler(enabled = route != FinishRoute.Main) { route = FinishRoute.Main }
    AnimatedContent(
        targetState = route,
        transitionSpec = {
            val forward = targetState.ordinal > initialState.ordinal
            if (forward) {
                (slideInHorizontally(tween(220)) { it / 3 } + fadeIn(tween(180))) togetherWith
                    (slideOutHorizontally(tween(220)) { -it / 4 } + fadeOut(tween(160)))
            } else {
                (slideInHorizontally(tween(220)) { -it / 3 } + fadeIn(tween(180))) togetherWith
                    (slideOutHorizontally(tween(220)) { it / 4 } + fadeOut(tween(160)))
            }
        },
        label = "finish-route",
    ) { current ->
        when (current) {
            FinishRoute.Main -> FinishMain(miuix, bottomInnerPadding, onBack) { route = FinishRoute.App }
            FinishRoute.App -> FinishAppPage(miuix, bottomInnerPadding) { route = FinishRoute.Main }
        }
    }
}

/* ---------------- main ---------------- */

@Composable
private fun FinishMain(
    miuix: Boolean,
    bottomInnerPadding: Dp,
    onBack: () -> Unit,
    onPickApp: () -> Unit,
) {
    val context = LocalContext.current
    var state by remember { mutableStateOf(FinishConfig.state) }
    var sortMode by remember { mutableStateOf(false) }
    fun commit(next: FinishState) {
        state = next
        FinishConfig.update(context, next)
    }
    val scriptPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        commit(state.copy(scriptUri = uri?.toString()))
    }
    val lkmPicker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        commit(state.copy(lkmUri = uri?.toString()))
    }
    val app = appDisplay(state.appPackage)

    if (miuix) {
        val scrollBehavior = MiuixScrollBehavior()
        MiuixScaffold(
            topBar = {
                MiuixTopAppBar(
                    title = stringResource(R.string.payload_title),
                    navigationIcon = { IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, null, tint = top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme.onBackground) } },
                    actions = {
                        IconButton(onClick = { sortMode = !sortMode }) {
                            Icon(
                                Icons.AutoMirrored.Filled.Sort,
                                contentDescription = stringResource(R.string.menu_sort),
                                tint = top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme.onBackground,
                            )
                        }
                    },
                    scrollBehavior = scrollBehavior,
                )
            },
            contentWindowInsets = WindowInsets.systemBars.add(WindowInsets.displayCutout).only(WindowInsetsSides.Horizontal),
        ) { inner ->
            val pageScroll = rememberScrollState()
            Column(modifier = Modifier.fillMaxSize().padding(inner)) {
                Column(
                    modifier = Modifier.weight(1f).fillMaxWidth()
                        .then(if (sortMode) Modifier else Modifier.verticalScroll(pageScroll))
                        .padding(horizontal = 16.dp, vertical = 12.dp)
                        .padding(bottom = bottomInnerPadding),
                    verticalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    if (sortMode) {
                        FinishSortList(miuix = true, order = state.order.filter { it in payloadIds }) { commit(state.copy(order = it + state.order.filter { id -> id !in payloadIds })) }
                    } else {
                        state.order.filter { it in payloadIds }.forEach { id ->
                            FinishOptionMiuix(id, state, ::commit, app, onPickApp, { scriptPicker.launch(arrayOf("text/x-shellscript", "text/plain", "application/octet-stream")) }, { lkmPicker.launch(arrayOf("application/octet-stream", "*/*")) })
                        }
                    }
                }
                /* 底部固定按钮：排序时是「完成」，否则是「保存」。 */
                top.yukonga.miuix.kmp.basic.Button(
                    modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 12.dp),
                    onClick = {
                        if (sortMode) {
                            sortMode = false
                        } else {
                            val file = FinishParams.save(context, state)
                            android.widget.Toast.makeText(context, context.getString(R.string.finish_saved, file.absolutePath), android.widget.Toast.LENGTH_LONG).show()
                        }
                    },
                ) {
                    top.yukonga.miuix.kmp.basic.Text(
                        stringResource(if (sortMode) R.string.finish_done else R.string.finish_save),
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
                        IconButton(onClick = { sortMode = !sortMode }) {
                            Icon(Icons.AutoMirrored.Filled.Sort, contentDescription = stringResource(R.string.menu_sort))
                        }
                    },
                    colors = expressiveTopAppBarColors(),
                    windowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
                    scrollBehavior = scrollBehavior,
                )
            },
            contentWindowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
        ) { inner ->
            val pageScroll = rememberScrollState()
            Column(modifier = Modifier.fillMaxSize().padding(inner)) {
                Column(
                    modifier = Modifier.weight(1f).fillMaxWidth()
                        .nestedScroll(scrollBehavior.nestedScrollConnection)
                        .then(if (sortMode) Modifier else Modifier.verticalScroll(pageScroll))
                        .padding(horizontal = 16.dp).padding(top = 16.dp, bottom = bottomInnerPadding),
                    verticalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    if (sortMode) {
                        TonalCard {
                            FinishSortList(miuix = false, order = state.order.filter { it in payloadIds }) { commit(state.copy(order = it + state.order.filter { id -> id !in payloadIds })) }
                        }
                    } else {
                        state.order.filter { it in payloadIds }.forEach { id ->
                            FinishOptionMaterial(id, state, ::commit, app, onPickApp, { scriptPicker.launch(arrayOf("text/x-shellscript", "text/plain", "application/octet-stream")) }, { lkmPicker.launch(arrayOf("application/octet-stream", "*/*")) })
                        }
                    }
                }
                androidx.compose.material3.Button(
                    modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 12.dp),
                    onClick = {
                        if (sortMode) {
                            sortMode = false
                        } else {
                            val file = FinishParams.save(contextM, state)
                            android.widget.Toast.makeText(contextM, contextM.getString(R.string.finish_saved, file.absolutePath), android.widget.Toast.LENGTH_LONG).show()
                        }
                    },
                ) {
                    Text(stringResource(if (sortMode) R.string.finish_done else R.string.finish_save))
                }
            }
        }
    }
}

/* ---------------- sort list ---------------- */

@Composable
private fun FinishSortList(miuix: Boolean, order: List<String>, onOrder: (List<String>) -> Unit) {
    val rowHeight = 60.dp
    val rowPx = with(LocalDensity.current) { rowHeight.toPx() }
    var draggingIndex by remember { mutableIntStateOf(-1) }
    var dragOffset by remember { mutableFloatStateOf(0f) }
    val latestOrder by rememberUpdatedState(order)
    LazyColumn(
        modifier = Modifier.fillMaxWidth().height(rowHeight * order.size),
        userScrollEnabled = false,
    ) {
        itemsIndexed(order, key = { _, id -> id }) { index, id ->
            val option = finishOptions.firstOrNull { it.id == id } ?: return@itemsIndexed
            val dragging = index == draggingIndex
            val rowShape = RoundedCornerShape(12.dp)
            val rowBg = if (miuix) {
                top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme.surface
            } else {
                MaterialTheme.colorScheme.surfaceVariant
            }
            val fg = if (miuix) top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme.onBackground else androidx.compose.ui.graphics.Color.Unspecified
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(rowHeight)
                    .then(
                        if (dragging) {
                            Modifier
                                .shadow(elevation = 6.dp, shape = rowShape, clip = false)
                                .background(rowBg, rowShape)
                        } else {
                            Modifier
                        },
                    )
                    .animateItem()
                    .zIndex(if (dragging) 1f else 0f)
                    .padding(start = 16.dp, end = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Icon(option.icon, contentDescription = null, tint = fg)
                Spacer(Modifier.width(16.dp))
                Text(stringResource(option.titleRes), modifier = Modifier.weight(1f), color = fg)
                Icon(
                    imageVector = Icons.Filled.DragHandle,
                    contentDescription = stringResource(R.string.finish_drag),
                    tint = fg,
                    modifier = Modifier
                        .size(48.dp)
                        .pointerInput(id) {
                            detectDragGestures(
                                onDragStart = { draggingIndex = latestOrder.indexOf(id); dragOffset = 0f },
                                onDragEnd = { draggingIndex = -1; dragOffset = 0f },
                                onDragCancel = { draggingIndex = -1; dragOffset = 0f },
                                onDrag = { change, amount ->
                                    change.consume()
                                    dragOffset += amount.y
                                    /* 自由拖动：一次手势内可连续跨过多行（while 消化所有越界）。 */
                                    var current = latestOrder.indexOf(id)
                                    while (dragOffset > rowPx / 2 && current < latestOrder.lastIndex) {
                                        val next = latestOrder.toMutableList().apply { add(current + 1, removeAt(current)) }
                                        onOrder(next)
                                        current += 1
                                        draggingIndex = current
                                        dragOffset -= rowPx
                                    }
                                    while (dragOffset < -rowPx / 2 && current > 0) {
                                        val next = latestOrder.toMutableList().apply { add(current - 1, removeAt(current)) }
                                        onOrder(next)
                                        current -= 1
                                        draggingIndex = current
                                        dragOffset += rowPx
                                    }
                                },
                            )
                        },
                )
            }
        }
    }
}

/* ---------------- option cards ---------------- */

@Composable
private fun FinishOptionMiuix(
    id: String,
    state: FinishState,
    commit: (FinishState) -> Unit,
    app: Pair<String, String>?,
    onPickApp: () -> Unit,
    launchScript: () -> Unit,
    launchLkm: () -> Unit,
) {
    val context = LocalContext.current
    when (id) {
        "root" -> top.yukonga.miuix.kmp.basic.Card(Modifier.fillMaxWidth().animateContentSize()) {
            SwitchPreference(
                checked = state.rootManagerEnabled,
                onCheckedChange = { commit(state.copy(rootManagerEnabled = it)) },
                title = stringResource(R.string.finish_root_manager),
                summary = if (state.rootManagerEnabled) {
                    state.rootManager + " · " + (FinishConfig.installedPackages(context, FinishConfig.spec(state.rootManager) ?: FinishConfig.rootManagers.first()).firstOrNull() ?: (FinishConfig.spec(state.rootManager) ?: FinishConfig.rootManagers.first()).packages.first())
                } else null,
                startAction = { Icon(Icons.Filled.AdminPanelSettings, null, Modifier.padding(end = 6.dp), tint = top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme.onBackground) },
            )
            if (state.rootManagerEnabled) {
                FinishConfig.rootManagers.forEach { spec ->
                    val installedPkgs = FinishConfig.installedPackages(context, spec)
                    val installed = installedPkgs.isNotEmpty()
                    RadioButtonPreference(
                        title = spec.name,
                        summary = if (installed) installedPkgs.first() else stringResource(R.string.finish_manager_missing, spec.name),
                        selected = state.rootManager == spec.name,
                        onClick = { commit(state.copy(rootManager = spec.name)) },
                        startAction = { ManagerIcon(spec) },
                        radioButtonLocation = RadioButtonLocation.End,
                        enabled = installed,
                    )
                }
            }
        }
        "app" -> top.yukonga.miuix.kmp.basic.Card(Modifier.fillMaxWidth().animateContentSize()) {
            SwitchPreference(
                checked = state.runAppEnabled,
                onCheckedChange = { commit(state.copy(runAppEnabled = it)) },
                title = stringResource(R.string.finish_run_app),
                summary = null,
                startAction = { Icon(Icons.Filled.Apps, null, Modifier.padding(end = 6.dp), tint = top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme.onBackground) },
            )
            if (state.runAppEnabled) {
                ArrowPreference(
                    title = app?.first ?: stringResource(R.string.finish_app_pick),
                    summary = app?.second ?: stringResource(R.string.finish_app_none),
                    onClick = onPickApp,
                    startAction = { AppLeadingIcon(state.appPackage) },
                )
            }
        }
        "cmd" -> top.yukonga.miuix.kmp.basic.Card(Modifier.fillMaxWidth().animateContentSize()) {
            SwitchPreference(
                checked = state.scriptEnabled,
                onCheckedChange = { commit(state.copy(scriptEnabled = it)) },
                title = stringResource(R.string.finish_script),
                summary = null,
                startAction = { Icon(Icons.Filled.Terminal, null, Modifier.padding(end = 6.dp), tint = top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme.onBackground) },
            )
            if (state.scriptEnabled) {
                RadioButtonPreference(
                    title = stringResource(R.string.finish_mode_command),
                    selected = state.scriptMode == 0,
                    onClick = { commit(state.copy(scriptMode = 0)) },
                    radioButtonLocation = RadioButtonLocation.End,
                )
                RadioButtonPreference(
                    title = stringResource(R.string.finish_mode_script),
                    selected = state.scriptMode == 1,
                    onClick = { commit(state.copy(scriptMode = 1)) },
                    radioButtonLocation = RadioButtonLocation.End,
                )
                if (state.scriptMode == 0) {
                    MiuixTextField(
                        value = state.command,
                        onValueChange = { commit(state.copy(command = it)) },
                        label = stringResource(R.string.finish_command_hint),
                        modifier = Modifier.padding(start = 16.dp, end = 16.dp, top = 8.dp, bottom = 16.dp),
                    )
                } else {
                    ArrowPreference(
                        title = stringResource(R.string.finish_script_pick),
                        summary = state.scriptUri ?: stringResource(R.string.finish_script_none),
                        onClick = launchScript,
                    )
                }
            }
        }
        "selinux" -> top.yukonga.miuix.kmp.basic.Card(Modifier.fillMaxWidth().animateContentSize()) {
            SwitchPreference(
                checked = state.restoreSelinux,
                onCheckedChange = { commit(state.copy(restoreSelinux = it)) },
                title = stringResource(R.string.finish_restore_selinux),
                summary = null,
                startAction = { Icon(Icons.Filled.Security, null, Modifier.padding(end = 6.dp), tint = top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme.onBackground) },
            )
        }
        "reboot" -> top.yukonga.miuix.kmp.basic.Card(Modifier.fillMaxWidth().animateContentSize()) {
            SwitchPreference(
                checked = state.softRebootAfterFinish,
                onCheckedChange = { commit(state.copy(softRebootAfterFinish = it)) },
                title = stringResource(R.string.finish_soft_reboot),
                summary = stringResource(R.string.finish_soft_reboot_summary),
                startAction = { Icon(Icons.Filled.RestartAlt, null, Modifier.padding(end = 6.dp), tint = top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme.onBackground) },
            )
        }
        "lkm" -> top.yukonga.miuix.kmp.basic.Card(Modifier.fillMaxWidth().animateContentSize()) {
            SwitchPreference(
                checked = state.lkmEnabled,
                onCheckedChange = { commit(state.copy(lkmEnabled = it)) },
                title = stringResource(R.string.finish_lkm),
                summary = if (state.lkmEnabled) state.lkmUri ?: stringResource(R.string.finish_lkm_none) else null,
                startAction = { Icon(Icons.Filled.Extension, null, Modifier.padding(end = 6.dp), tint = top.yukonga.miuix.kmp.theme.MiuixTheme.colorScheme.onBackground) },
            )
            if (state.lkmEnabled) {
                ArrowPreference(
                    title = stringResource(R.string.finish_lkm_pick),
                    summary = state.lkmUri ?: stringResource(R.string.finish_lkm_none),
                    onClick = launchLkm,
                )

            }
        }
    }
}

@Composable
private fun FinishOptionMaterial(
    id: String,
    state: FinishState,
    commit: (FinishState) -> Unit,
    app: Pair<String, String>?,
    onPickApp: () -> Unit,
    launchScript: () -> Unit,
    launchLkm: () -> Unit,
) {
    val context = LocalContext.current
    when (id) {
        "root" -> TonalCard {
            SegmentedColumn(Modifier.fillMaxWidth()) {
                item {
                    SegmentedListItem(
                        leadingContent = { Icon(Icons.Filled.AdminPanelSettings, null) },
                        headlineContent = { Text(stringResource(R.string.finish_root_manager)) },
                        supportingContent = if (state.rootManagerEnabled) {
                            { Text(state.rootManager + " · " + (FinishConfig.installedPackages(context, FinishConfig.spec(state.rootManager) ?: FinishConfig.rootManagers.first()).firstOrNull() ?: (FinishConfig.spec(state.rootManager) ?: FinishConfig.rootManagers.first()).packages.first())) }
                        } else null,
                        trailingContent = { Switch(checked = state.rootManagerEnabled, onCheckedChange = { commit(state.copy(rootManagerEnabled = it)) }) },
                    )
                }
                if (state.rootManagerEnabled) {
                    FinishConfig.rootManagers.forEach { spec ->
                        val installedPkgs = FinishConfig.installedPackages(context, spec)
                        val installed = installedPkgs.isNotEmpty()
                        item {
                            SegmentedListItem(
                                enabled = installed,
                                onClick = if (installed) ({ commit(state.copy(rootManager = spec.name)) }) else null,
                                leadingContent = { ManagerIcon(spec) },
                                headlineContent = { Text(spec.name) },
                                supportingContent = { Text(if (installed) installedPkgs.first() else stringResource(R.string.finish_manager_missing, spec.name)) },
                                trailingContent = { RadioButton(selected = state.rootManager == spec.name, onClick = null, enabled = installed) },
                            )
                        }
                    }
                }
            }
        }
        "app" -> TonalCard {
            SegmentedColumn(Modifier.fillMaxWidth()) {
                item {
                    SegmentedListItem(
                        leadingContent = { Icon(Icons.Filled.Apps, null) },
                        headlineContent = { Text(stringResource(R.string.finish_run_app)) },
                        trailingContent = { Switch(checked = state.runAppEnabled, onCheckedChange = { commit(state.copy(runAppEnabled = it)) }) },
                    )
                }
                if (state.runAppEnabled) {
                    item {
                        SegmentedListItem(
                            onClick = onPickApp,
                            leadingContent = { AppLeadingIcon(state.appPackage) },
                            headlineContent = { Text(app?.first ?: stringResource(R.string.finish_app_pick)) },
                            supportingContent = { Text(app?.second ?: stringResource(R.string.finish_app_none)) },
                            trailingContent = { Icon(Icons.AutoMirrored.Filled.KeyboardArrowRight, null) },
                        )
                    }
                }
            }
        }
        "cmd" -> TonalCard {
            SegmentedColumn(Modifier.fillMaxWidth()) {
                item {
                    SegmentedListItem(
                        leadingContent = { Icon(Icons.Filled.Terminal, null) },
                        headlineContent = { Text(stringResource(R.string.finish_script)) },
                        trailingContent = { Switch(checked = state.scriptEnabled, onCheckedChange = { commit(state.copy(scriptEnabled = it)) }) },
                    )
                }
                if (state.scriptEnabled) {
                    item {
                        SegmentedListItem(
                            onClick = { commit(state.copy(scriptMode = 0)) },
                            headlineContent = { Text(stringResource(R.string.finish_mode_command)) },
                            trailingContent = { RadioButton(selected = state.scriptMode == 0, onClick = null) },
                        )
                    }
                    item {
                        SegmentedListItem(
                            onClick = { commit(state.copy(scriptMode = 1)) },
                            headlineContent = { Text(stringResource(R.string.finish_mode_script)) },
                            trailingContent = { RadioButton(selected = state.scriptMode == 1, onClick = null) },
                        )
                    }
                    if (state.scriptMode == 0) {
                        item { OutlinedTextField(state.command, { commit(state.copy(command = it)) }, label = { Text(stringResource(R.string.finish_command_hint)) }, modifier = Modifier.fillMaxWidth().padding(start = 16.dp, end = 16.dp, top = 8.dp, bottom = 16.dp), singleLine = true) }
                    } else {
                        item { SegmentedListItem(onClick = launchScript, headlineContent = { Text(stringResource(R.string.finish_script_pick)) }, supportingContent = { Text(state.scriptUri ?: stringResource(R.string.finish_script_none)) }) }
                    }
                }
            }
        }
        "selinux" -> TonalCard {
            SegmentedColumn(Modifier.fillMaxWidth()) {
                item {
                    SegmentedListItem(
                        leadingContent = { Icon(Icons.Filled.Security, null) },
                        headlineContent = { Text(stringResource(R.string.finish_restore_selinux)) },
                        trailingContent = { Switch(checked = state.restoreSelinux, onCheckedChange = { commit(state.copy(restoreSelinux = it)) }) },
                    )
                }
            }
        }
        "reboot" -> TonalCard {
            SegmentedColumn(Modifier.fillMaxWidth()) {
                item {
                    SegmentedListItem(
                        leadingContent = { Icon(Icons.Filled.RestartAlt, null) },
                        headlineContent = { Text(stringResource(R.string.finish_soft_reboot)) },
                        supportingContent = { Text(stringResource(R.string.finish_soft_reboot_summary)) },
                        trailingContent = {
                            Switch(
                                checked = state.softRebootAfterFinish,
                                onCheckedChange = { commit(state.copy(softRebootAfterFinish = it)) },
                            )
                        },
                    )
                }
            }
        }
        "lkm" -> TonalCard {
            SegmentedColumn(Modifier.fillMaxWidth()) {
                item {
                    SegmentedListItem(
                        leadingContent = { Icon(Icons.Filled.Extension, null) },
                        headlineContent = { Text(stringResource(R.string.finish_lkm)) },
                        trailingContent = { Switch(checked = state.lkmEnabled, onCheckedChange = { commit(state.copy(lkmEnabled = it)) }) },
                    )
                }
                if (state.lkmEnabled) {
                    item { SegmentedListItem(onClick = launchLkm, headlineContent = { Text(stringResource(R.string.finish_lkm_pick)) }, supportingContent = { Text(state.lkmUri ?: stringResource(R.string.finish_lkm_none)) }) }
                    item { OutlinedTextField(state.lkmParams, { commit(state.copy(lkmParams = it)) }, label = { Text(stringResource(R.string.finish_lkm_params)) }, modifier = Modifier.fillMaxWidth().padding(start = 16.dp, end = 16.dp, top = 8.dp, bottom = 16.dp), singleLine = true) }

                }
            }
        }
    }
}

/* ---------------- helpers ---------------- */

@Composable
private fun ManagerIcon(spec: FinishConfig.RootManagerSpec) {
    val context = LocalContext.current
    val installed = FinishConfig.installedPackages(context, spec)
    val pkg = installed.firstOrNull() ?: spec.packages.first()
    val info = remember(pkg) { runCatching { context.packageManager.getApplicationInfo(pkg, 0) }.getOrNull() }
    if (info != null) {
        AppIconImage(Modifier.size(36.dp), info, info.loadLabel(context.packageManager).toString())
    } else {
        Box(Modifier.size(36.dp), contentAlignment = Alignment.Center) {
            Icon(Icons.Filled.HelpOutline, null, Modifier.size(20.dp), tint = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

@Composable
private fun AppLeadingIcon(pkg: String?) {
    val context = LocalContext.current
    val info = remember(pkg) { pkg?.let { runCatching { context.packageManager.getApplicationInfo(it, 0) }.getOrNull() } }
    if (info != null) {
        AppIconImage(Modifier.size(36.dp), info, info.loadLabel(context.packageManager).toString())
    } else {
        Icon(Icons.Filled.Apps, null, Modifier.size(24.dp), tint = MaterialTheme.colorScheme.onSurfaceVariant)
    }
}


@Composable
private fun appDisplay(pkg: String?): Pair<String, String>? {
    val context = LocalContext.current
    if (pkg.isNullOrBlank()) return null
    return remember(pkg) {
        runCatching {
            val info = context.packageManager.getApplicationInfo(pkg, 0)
            info.loadLabel(context.packageManager).toString() to pkg
        }.getOrNull()
    }
}

/* ---------------- app picker ---------------- */

private enum class AppSort { Name, Package, Installed, Updated }

private data class AppEntry(
    val info: ApplicationInfo,
    val label: String,
    val system: Boolean,
    val installed: Long,
    val updated: Long,
)

@Composable
private fun sortLabel(sort: AppSort): String = when (sort) {
    AppSort.Name -> stringResource(R.string.finish_sort_name)
    AppSort.Package -> stringResource(R.string.finish_sort_package)
    AppSort.Installed -> stringResource(R.string.finish_sort_installed)
    AppSort.Updated -> stringResource(R.string.finish_sort_updated)
}

@Composable
private fun FinishAppPage(miuix: Boolean, bottomInnerPadding: Dp, onBack: () -> Unit) {
    val context = LocalContext.current
    var state by remember { mutableStateOf(FinishConfig.state) }
    var query by remember { mutableStateOf("") }
    var sort by remember { mutableStateOf(AppSort.Name) }
    var desc by remember { mutableStateOf(false) }
    var showSystem by remember { mutableStateOf(false) }

    val allApps = remember {
        val pm = context.packageManager
        @Suppress("DEPRECATION")
        pm.getInstalledApplications(PackageManager.GET_META_DATA)
            .filter { it.packageName != context.packageName }
            .filter { info -> pm.getLaunchIntentForPackage(info.packageName) != null || (info.flags and ApplicationInfo.FLAG_SYSTEM) == 0 }
            .map { info ->
                val pkg = runCatching { pm.getPackageInfo(info.packageName, 0) }.getOrNull()
                AppEntry(info, info.loadLabel(pm).toString(), (info.flags and ApplicationInfo.FLAG_SYSTEM) != 0, pkg?.firstInstallTime ?: 0L, pkg?.lastUpdateTime ?: 0L)
            }
    }
    val apps = remember(query, sort, desc, showSystem, allApps) {
        val filtered = allApps.filter { app ->
            (showSystem || !app.system) && (query.isBlank() || app.label.contains(query, true) || app.info.packageName.contains(query, true))
        }
        val comparator: Comparator<AppEntry> = when (sort) {
            AppSort.Name -> compareBy { it.label.lowercase() }
            AppSort.Package -> compareBy { it.info.packageName }
            AppSort.Installed -> compareBy { it.installed }
            AppSort.Updated -> compareBy { it.updated }
        }
        if (desc) filtered.sortedWith(comparator.reversed()) else filtered.sortedWith(comparator)
    }
    val onPick: (AppEntry) -> Unit = { app ->
        val next = state.copy(appPackage = app.info.packageName)
        state = next
        FinishConfig.update(context, next)
        onBack()
    }
    val actions: @Composable () -> Unit = {
        FinishSortAction(miuix, sort, desc, { sort = it }, { desc = !desc })
        FinishFilterAction(miuix, showSystem) { showSystem = !showSystem }
    }

    if (miuix) {
        MiuixScaffold(
            topBar = {
                MiuixTopAppBar(
                    title = stringResource(R.string.finish_app_pick),
                    navigationIcon = { IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, null) } },
                    actions = { actions() },
                )
            },
            contentWindowInsets = WindowInsets.systemBars.add(WindowInsets.displayCutout).only(WindowInsetsSides.Horizontal),
        ) { inner ->
            Column(Modifier.fillMaxSize().padding(inner)) {
                MiuixTextField(query, { query = it }, label = stringResource(R.string.finish_app_search), modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp))
                LazyColumn(Modifier.fillMaxSize().padding(bottom = bottomInnerPadding)) {
                    items(apps, key = { it.info.packageName }) { app ->
                        top.yukonga.miuix.kmp.basic.Card(Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 4.dp)) {
                            top.yukonga.miuix.kmp.basic.BasicComponent(
                                title = app.label,
                                summary = app.info.packageName,
                                startAction = { AppIconImage(Modifier.size(40.dp), app.info, app.label) },
                                endActions = { if (state.appPackage == app.info.packageName) Icon(Icons.Filled.Check, null) },
                                onClick = { onPick(app) },
                            )
                        }
                    }
                }
            }
        }
    } else {
        Scaffold(
            topBar = {
                TopAppBar(
                    title = { Text(stringResource(R.string.finish_app_pick)) },
                    navigationIcon = { IconButton(onClick = onBack) { Icon(Icons.AutoMirrored.Filled.ArrowBack, null) } },
                    actions = { actions() },
                )
            },
        ) { inner ->
            Column(Modifier.fillMaxSize().padding(inner)) {
                OutlinedTextField(query, { query = it }, label = { Text(stringResource(R.string.finish_app_search)) }, modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp), singleLine = true)
                LazyColumn(Modifier.fillMaxSize().padding(bottom = bottomInnerPadding)) {
                    items(apps, key = { it.info.packageName }) { app ->
                        Row(Modifier.fillMaxWidth().clickable { onPick(app) }.padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
                            AppIconImage(Modifier.size(40.dp), app.info, app.label)
                            Column(Modifier.padding(start = 12.dp).weight(1f)) {
                                Text(app.label)
                                Text(app.info.packageName, style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant)
                            }
                            if (state.appPackage == app.info.packageName) Icon(Icons.Filled.Check, null)
                        }
                    }
                }
            }
        }
    }
}

/* ---------------- menus ---------------- */

@Composable
private fun FinishSortAction(miuix: Boolean, sort: AppSort, desc: Boolean, onSort: (AppSort) -> Unit, onToggleDesc: () -> Unit) {
    var show by remember { mutableStateOf(false) }
    if (miuix) {
        Box {
            IconButton(onClick = { show = true }) { Icon(Icons.AutoMirrored.Filled.Sort, null) }
            top.yukonga.miuix.kmp.overlay.OverlayListPopup(
                show = show,
                popupPositionProvider = com.ghostlock.app.ui.component.ListPopupDefaults.MenuPositionProvider,
                alignment = top.yukonga.miuix.kmp.basic.PopupPositionProvider.Align.TopEnd,
                onDismissRequest = { show = false },
                content = {
                    top.yukonga.miuix.kmp.basic.ListPopupColumn {
                        AppSort.entries.forEachIndexed { index, option ->
                            top.yukonga.miuix.kmp.basic.DropdownImpl(
                                text = sortLabel(option),
                                optionSize = AppSort.entries.size + 1,
                                isSelected = sort == option,
                                index = index,
                                onSelectedIndexChange = { onSort(option); show = false },
                            )
                        }
                        top.yukonga.miuix.kmp.basic.HorizontalDivider(Modifier.padding(horizontal = 20.dp, vertical = 4.dp), thickness = 1.5.dp)
                        top.yukonga.miuix.kmp.basic.DropdownImpl(
                            text = stringResource(R.string.finish_sort_desc),
                            optionSize = AppSort.entries.size + 1,
                            isSelected = desc,
                            index = AppSort.entries.size,
                            onSelectedIndexChange = { onToggleDesc(); show = false },
                        )
                    }
                },
            )
        }
    } else {
        IconButton(onClick = { show = true }) {
            Icon(Icons.AutoMirrored.Filled.Sort, null)
            DropdownMenuPopup(expanded = show, onDismissRequest = { show = false }) {
                DropdownMenuGroup(shapes = MenuDefaults.groupShape(index = 0, count = 2)) {
                    AppSort.entries.forEachIndexed { index, option ->
                        SelectableDropdownMenuItem(
                            text = { Text(sortLabel(option)) },
                            selected = sort == option,
                            selectedLeadingIcon = { Icon(Icons.Filled.Check, null, Modifier.size(MenuDefaults.LeadingIconSize)) },
                            onClick = { onSort(option); show = false },
                            shapes = MenuDefaults.itemShape(index = index, count = AppSort.entries.size),
                        )
                    }
                }
                Spacer(Modifier.size(MenuDefaults.GroupSpacing))
                DropdownMenuGroup(shapes = MenuDefaults.groupShape(index = 1, count = 2)) {
                    CheckableDropdownMenuItem(
                        text = { Text(stringResource(R.string.finish_sort_desc)) },
                        checked = desc,
                        checkedLeadingIcon = { Icon(Icons.Filled.Check, null, Modifier.size(MenuDefaults.LeadingIconSize)) },
                        onCheckedChange = { onToggleDesc(); show = false },
                        shapes = MenuDefaults.itemShape(index = 0, count = 1),
                    )
                }
            }
        }
    }
}

@Composable
private fun FinishFilterAction(miuix: Boolean, showSystem: Boolean, onToggleSystem: () -> Unit) {
    var show by remember { mutableStateOf(false) }
    if (miuix) {
        Box {
            IconButton(onClick = { show = true }) { Icon(Icons.Filled.FilterList, null) }
            top.yukonga.miuix.kmp.overlay.OverlayListPopup(
                show = show,
                popupPositionProvider = com.ghostlock.app.ui.component.ListPopupDefaults.MenuPositionProvider,
                alignment = top.yukonga.miuix.kmp.basic.PopupPositionProvider.Align.TopEnd,
                onDismissRequest = { show = false },
                content = {
                    top.yukonga.miuix.kmp.basic.ListPopupColumn {
                        top.yukonga.miuix.kmp.basic.DropdownImpl(
                            text = stringResource(R.string.finish_show_system),
                            optionSize = 1,
                            isSelected = showSystem,
                            index = 0,
                            onSelectedIndexChange = { onToggleSystem(); show = false },
                        )
                    }
                },
            )
        }
    } else {
        IconButton(onClick = { show = true }) {
            Icon(Icons.Filled.FilterList, null)
            DropdownMenuPopup(expanded = show, onDismissRequest = { show = false }) {
                DropdownMenuGroup(shapes = MenuDefaults.groupShapes()) {
                    CheckableDropdownMenuItem(
                        text = { Text(stringResource(R.string.finish_show_system)) },
                        checked = showSystem,
                        checkedLeadingIcon = { Icon(Icons.Filled.Check, null, Modifier.size(MenuDefaults.LeadingIconSize)) },
                        onCheckedChange = { onToggleSystem(); show = false },
                        shapes = MenuDefaults.itemShape(index = 0, count = 1),
                    )
                }
            }
        }
    }
}
