package com.ghostlock.app.ui

import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.requiredSize
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.CheckCircleOutline
import androidx.compose.material.icons.rounded.RemoveCircleOutline
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.colorResource
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ghostlock.app.BuildConfig
import com.ghostlock.app.BuildInfo
import com.ghostlock.app.R
import com.ghostlock.app.domain.model.ExecutionFieldValue
import com.ghostlock.app.domain.model.ProfileFieldNode
import com.ghostlock.app.domain.model.ShizukuStatus
import top.yukonga.miuix.kmp.basic.ButtonDefaults
import top.yukonga.miuix.kmp.basic.Card
import top.yukonga.miuix.kmp.basic.CardDefaults
import top.yukonga.miuix.kmp.basic.DropdownItem
import top.yukonga.miuix.kmp.basic.Icon
import top.yukonga.miuix.kmp.basic.IconButton
import top.yukonga.miuix.kmp.basic.MiuixScrollBehavior
import top.yukonga.miuix.kmp.basic.Scaffold
import top.yukonga.miuix.kmp.basic.ScrollBehavior
import top.yukonga.miuix.kmp.basic.Text
import top.yukonga.miuix.kmp.basic.TextButton
import top.yukonga.miuix.kmp.basic.TextField
import top.yukonga.miuix.kmp.basic.TextFieldDefaults
import top.yukonga.miuix.kmp.basic.TopAppBar
import top.yukonga.miuix.kmp.basic.rememberTopAppBarState
import top.yukonga.miuix.kmp.icon.MiuixIcons
import top.yukonga.miuix.kmp.icon.extended.Close
import top.yukonga.miuix.kmp.icon.extended.Copy
import top.yukonga.miuix.kmp.overlay.OverlayBottomSheet
import top.yukonga.miuix.kmp.overlay.OverlayDialog
import top.yukonga.miuix.kmp.preference.ArrowPreference
import top.yukonga.miuix.kmp.preference.OverlaySpinnerPreference
import top.yukonga.miuix.kmp.preference.SwitchPreference
import top.yukonga.miuix.kmp.theme.MiuixTheme
import top.yukonga.miuix.kmp.theme.darkColorScheme
import top.yukonga.miuix.kmp.theme.lightColorScheme
import top.yukonga.miuix.kmp.utils.overScrollVertical

data class GhostlockUiState(
    val deviceName: String = "",
    val kernelRelease: String = "",
    val socName: String = "",
    val kernelSupported: Boolean = false,
    val shizukuEnabled: Boolean = false,
    val shizukuStatus: ShizukuStatus = ShizukuStatus.NOT_REQUIRED,
    val running: Boolean = false,
    val exportVisible: Boolean = false,
    val cpuPairLabels: List<String> = emptyList(),
    val cpuPairIndex: Int = 0,
    val safeModeEnabled: Boolean = false,
    val executionSheetVisible: Boolean = false,
    val executionSheetDismissible: Boolean = false,
    val dialogVisible: Boolean = false,
    val dialogType: DialogType = DialogType.NONE,
    val dialogTitleRes: Int = 0,
    val dialogMessage: String = "",
    val dialogMessageRes: Int = 0,
    val dialogItems: List<String> = emptyList(),
    val dialogItemResIds: List<Int> = emptyList(),
    val dialogCurrentItemIndex: Int = -1,
    val dialogInput: String = "",
    val overwriteDialogVisible: Boolean = false,
    val overwriteMessage: String = "",
    val logLines: List<GhostlockLogLine> = emptyList(),
    val executionRelease: String = "",
    val executionHasProfile: Boolean = false,
    val executionFields: List<ExecutionFieldValue> = emptyList(),
    val executionEditing: Map<String, String> = emptyMap(),
    val advancedScreenVisible: Boolean = false,
    val debugExportEnabled: Boolean = true,
    val debugExportLocation: String = "",
    val debugKernelLogEnabled: Boolean = true,
    val aboutVisible: Boolean = false,
    val parametersVisible: Boolean = false,
    val profileOverrideVisible: Boolean = false,
    val advancedOverrideVisible: Boolean = false,
    val profileOverrideRelease: String = "",
    val profileOverrideRoots: List<ProfileFieldNode> = emptyList(),
    val profileOverrideEditing: Map<String, String> = emptyMap(),
    /** Controller-reported geometry violations, dotted paths. */
    val profileInvalidPaths: Set<String> = emptySet(),
    /** Explicit route from the profile; null means geometry inference. */
    val profileRoute: String? = null,
    /** Declared fallback route; null/"none" means disabled. */
    val profileFallback: String? = null,
    /** Manually selected builtin source; null means automatic matching. */
    val activeBuiltinProfile: String? = null,
    val builtinScreenVisible: Boolean = false,
    /** Unfilled reference templates, listed separately on the builtin picker. */
    val builtinTemplates: List<String> = emptyList(),
    /** Builtin releases sorted by similarity to the device kernel. */
    val builtinProfiles: List<String> = emptyList(),
)

enum class DialogType { NONE, LIST, INPUT, CONFIRM }

data class GhostlockLogLine(val text: String, val color: Int)

interface GhostlockActions {
    fun onRun()
    fun onProfileInvalid()
    fun onStatusClick()
    fun onCloseExecutionSheet()
    fun onCopyLogs()
    fun onImportOffsetsHocon()
    fun onImportOffsetsJson()
    fun onDocumentsResult(request: DocumentRequest, uris: List<String>)
    fun onParseOta()
    fun onParseImage()
    fun onExportOffsets()
    fun onCpuPairSelected(index: Int)
    fun onSafeModeChanged(enabled: Boolean)
    fun onShizukuChanged(enabled: Boolean)
    fun onDialogItemSelected(index: Int)
    fun onDialogInputChange(value: String)
    fun onDialogConfirm(value: String)
    fun onDialogDismiss()
    fun onDialogDismissFinished()
    fun onOverwriteConfirm()
    fun onOverwriteDismiss()
    fun onExecutionFieldChanged(path: String, value: String)
    fun onRouteChanged(index: Int)
    fun onFallbackChanged(index: Int)
    fun onExportProfile()
    fun onResetParameters()
    fun onOpenAdvanced()
    fun onCloseAdvanced()
    fun onShowAbout()
    fun onCloseAbout()
    fun onDebugExportChanged(enabled: Boolean)
    fun onDebugExportLocationPick()
    fun onDebugKernelLogChanged(enabled: Boolean)
    fun onOpenParameters()
    fun onCloseParameters()
    fun onOpenBuiltinProfiles()
    fun onCloseBuiltinProfiles()
    fun onSelectBuiltinProfile(release: String?)
    fun onOpenProfileOverrides()
    fun onCloseProfileOverrides()
    fun onOpenAdvancedOverrides()
    fun onCloseAdvancedOverrides()
    fun onProfileOverrideChanged(path: String, value: String)
}

private enum class GhostlockScreen(val depth: Int) {
    Main(0),
    Advanced(1),
    Parameters(2),
    Builtin(3),
    ProfileOverride(4),
    AdvancedOverride(5),
}

@Composable
internal fun GhostlockApp(
    state: GhostlockUiState,
    actions: GhostlockActions,
) {
    MiuixTheme(
        colors = if (isSystemInDarkTheme()) darkColorScheme() else lightColorScheme(),
    ) {
        Scaffold(modifier = Modifier.fillMaxSize()) {
            val screen = when {
                !state.advancedScreenVisible -> GhostlockScreen.Main
                state.advancedOverrideVisible -> GhostlockScreen.AdvancedOverride
                state.profileOverrideVisible -> GhostlockScreen.ProfileOverride
                state.builtinScreenVisible -> GhostlockScreen.Builtin
                state.parametersVisible -> GhostlockScreen.Parameters
                else -> GhostlockScreen.Advanced
            }
            BackHandler(enabled = screen != GhostlockScreen.Main && !state.aboutVisible) {
                when (screen) {
                    GhostlockScreen.AdvancedOverride -> actions.onCloseAdvancedOverrides()
                    GhostlockScreen.ProfileOverride -> actions.onCloseProfileOverrides()
                    GhostlockScreen.Builtin -> actions.onCloseBuiltinProfiles()
                    GhostlockScreen.Parameters -> actions.onCloseParameters()
                    GhostlockScreen.Advanced -> actions.onCloseAdvanced()
                    GhostlockScreen.Main -> Unit
                }
            }
            AnimatedContent(
                targetState = screen,
                transitionSpec = {
                    if (targetState.depth > initialState.depth) {
                        (slideInHorizontally { it } + fadeIn()) togetherWith
                            (slideOutHorizontally { -it / 3 } + fadeOut())
                    } else {
                        (slideInHorizontally { -it / 3 } + fadeIn()) togetherWith
                            (slideOutHorizontally { it } + fadeOut())
                    }
                },
                label = "ghostlock-screen",
            ) { target ->
                when (target) {
                    GhostlockScreen.Main -> MainScreen(state = state, actions = actions)
                    GhostlockScreen.Advanced -> AdvancedScreen(state = state, actions = actions)
                    GhostlockScreen.Parameters ->
                        ParameterScreen(state = state, actions = actions)
                    GhostlockScreen.Builtin ->
                        BuiltinProfileScreen(state = state, actions = actions)
                    GhostlockScreen.ProfileOverride ->
                        ProfileOverrideScreen(state = state, actions = actions)
                    GhostlockScreen.AdvancedOverride ->
                        AdvancedOverrideScreen(state = state, actions = actions)
                }
            }
            GhostlockDialog(state = state, actions = actions)
            GhostlockOverwriteDialog(state = state, actions = actions)
            GhostlockExecutionSheet(state = state, actions = actions)
        }
    }
}

@Composable
private fun MainScreen(
    state: GhostlockUiState,
    actions: GhostlockActions,
) {
    val scrollBehavior = MiuixScrollBehavior(rememberTopAppBarState())
    Scaffold(
        modifier = Modifier.fillMaxSize(),
        topBar = {
            TopAppBar(
                title = "GhostLock",
                scrollBehavior = scrollBehavior,
            )
        },
    ) { paddingValues ->
        BoxWithConstraints(
            modifier = Modifier
                .fillMaxSize(),
        ) {
            if (maxWidth < 768.dp) {
                PortraitContent(
                    state = state,
                    actions = actions,
                    scrollBehavior = scrollBehavior,
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(paddingValues),
                )
            } else {
                LandscapeContent(
                    state = state,
                    actions = actions,
                    scrollBehavior = scrollBehavior,
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(paddingValues),
                )
            }
        }
    }
}

@Composable
internal fun GhostlockAboutDialog(
    show: Boolean,
    onDismissRequest: () -> Unit,
) {
    OverlayDialog(
        show = show,
        title = stringResource(R.string.about),
        onDismissRequest = onDismissRequest,
        content = {
            val uriHandler = LocalUriHandler.current
            Row(
                modifier = Modifier.padding(bottom = 10.dp),
                horizontalArrangement = Arrangement.spacedBy(16.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Box(
                    modifier = Modifier
                        .size(45.dp)
                        .clip(RoundedCornerShape(12.dp))
                        .background(colorResource(R.color.launcher_icon_background)),
                    contentAlignment = Alignment.Center,
                ) {
                    Image(
                        painter = painterResource(R.drawable.ic_launcher_foreground),
                        contentDescription = stringResource(R.string.app_name),
                        modifier = Modifier.requiredSize(67.dp),
                    )
                }
                Column {
                    Text(
                        text = stringResource(R.string.app_name),
                        fontSize = 22.sp,
                        fontWeight = FontWeight.SemiBold,
                    )
                    Text(text = "${BuildConfig.VERSION_NAME} (${BuildConfig.VERSION_CODE})")
                }
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(text = stringResource(R.string.view_source) + " ")
                Text(
                    text = AnnotatedString(
                        text = "GitHub",
                        spanStyle = SpanStyle(
                            textDecoration = TextDecoration.Underline,
                            color = MiuixTheme.colorScheme.primary,
                        ),
                    ),
                    modifier = Modifier.clickable {
                        uriHandler.openUri("https://github.com/YuKongA/ghostlock-app")
                    },
                )
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(text = stringResource(R.string.join_channel) + " ")
                Text(
                    text = AnnotatedString(
                        text = "Telegram",
                        spanStyle = SpanStyle(
                            textDecoration = TextDecoration.Underline,
                            color = MiuixTheme.colorScheme.primary,
                        ),
                    ),
                    modifier = Modifier.clickable {
                        uriHandler.openUri("https://t.me/YuKongA13579")
                    },
                )
            }
            Text(
                modifier = Modifier.padding(top = 10.dp),
                text = stringResource(R.string.opensource_info),
            )
        },
    )
}

@Composable
private fun GhostlockExecutionSheet(
    state: GhostlockUiState,
    actions: GhostlockActions,
) {
    OverlayBottomSheet(
        show = state.executionSheetVisible,
        title = stringResource(R.string.log_title),
        allowDismiss = state.executionSheetDismissible,
        onDismissRequest = actions::onCloseExecutionSheet,
        startAction = {
            IconButton(onClick = actions::onCopyLogs) {
                Icon(
                    imageVector = MiuixIcons.Copy,
                    contentDescription = stringResource(R.string.action_copy),
                    tint = MiuixTheme.colorScheme.onBackground,
                )
            }
        },
        endAction = {
            IconButton(
                enabled = state.executionSheetDismissible,
                onClick = actions::onCloseExecutionSheet,
            ) {
                Icon(
                    imageVector = MiuixIcons.Close,
                    contentDescription = stringResource(R.string.action_close),
                )
            }
        },
        content = {
            LogPanel(
                lines = state.logLines,
                modifier = Modifier
                    .fillMaxWidth()
                    .heightIn(min = 240.dp, max = 520.dp)
                    .navigationBarsPadding(),
            )
        },
    )
}

@Composable
private fun GhostlockDialog(
    state: GhostlockUiState,
    actions: GhostlockActions,
) {
    OverlayDialog(
        show = state.dialogVisible,
        title = if (state.dialogType == DialogType.NONE) null else stringResource(state.dialogTitleRes),
        onDismissRequest = actions::onDialogDismiss,
        onDismissFinished = actions::onDialogDismissFinished,
        content = {
            when (state.dialogType) {
                DialogType.LIST -> {
                    val items = state.dialogItems.ifEmpty { state.dialogItemResIds.map { stringResource(it) } }
                    items.forEachIndexed { index, item ->
                        TextButton(
                            modifier = Modifier
                                .fillMaxWidth()
                                .padding(bottom = 12.dp),
                            text = if (index == state.dialogCurrentItemIndex) {
                                stringResource(R.string.export_current_marker, item)
                            } else {
                                item
                            },
                            onClick = { actions.onDialogItemSelected(index) },
                        )
                    }
                    TextButton(
                        modifier = Modifier.fillMaxWidth(),
                        text = stringResource(R.string.cancel),
                        onClick = actions::onDialogDismiss,
                    )
                }

                DialogType.INPUT -> {
                    TextField(
                        value = state.dialogInput,
                        onValueChange = actions::onDialogInputChange,
                        label = stringResource(state.dialogMessageRes),
                        modifier = Modifier.fillMaxWidth(),
                        singleLine = true,
                    )
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(top = 16.dp)
                    ) {
                        TextButton(
                            modifier = Modifier.weight(1f),
                            text = stringResource(R.string.cancel),
                            onClick = actions::onDialogDismiss,
                        )
                        Spacer(modifier = Modifier.width(12.dp))
                        TextButton(
                            modifier = Modifier.weight(1f),
                            text = stringResource(R.string.parse_start),
                            colors = ButtonDefaults.textButtonColorsPrimary(),
                            onClick = { actions.onDialogConfirm(state.dialogInput) },
                        )
                    }
                }

                DialogType.CONFIRM -> {
                    Text(
                        text = stringResource(state.dialogMessageRes),
                        modifier = Modifier.fillMaxWidth(),
                        style = MiuixTheme.textStyles.body2,
                        color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                    )
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(top = 16.dp),
                    ) {
                        TextButton(
                            modifier = Modifier.weight(1f),
                            text = stringResource(R.string.cancel),
                            onClick = actions::onDialogDismiss,
                        )
                        Spacer(modifier = Modifier.width(12.dp))
                        TextButton(
                            modifier = Modifier.weight(1f),
                            text = stringResource(R.string.w3_shizuku_hint_enable),
                            colors = ButtonDefaults.textButtonColorsPrimary(),
                            onClick = { actions.onDialogConfirm("") },
                        )
                    }
                }

                DialogType.NONE -> Unit
            }
        },
    )
}

@Composable
private fun GhostlockOverwriteDialog(
    state: GhostlockUiState,
    actions: GhostlockActions,
) {
    OverlayDialog(
        show = state.overwriteDialogVisible,
        title = stringResource(R.string.overwrite_title),
        summary = stringResource(R.string.overwrite_message, state.overwriteMessage),
        onDismissRequest = actions::onOverwriteDismiss,
        content = {
            Row(modifier = Modifier.fillMaxWidth()) {
                TextButton(
                    modifier = Modifier.weight(1f),
                    text = stringResource(R.string.cancel),
                    onClick = actions::onOverwriteDismiss,
                )
                Spacer(modifier = Modifier.width(12.dp))
                TextButton(
                    modifier = Modifier.weight(1f),
                    text = stringResource(R.string.overwrite_yes),
                    colors = ButtonDefaults.textButtonColorsPrimary(),
                    onClick = actions::onOverwriteConfirm,
                )
            }
        },
    )
}

@Composable
private fun PortraitContent(
    state: GhostlockUiState,
    actions: GhostlockActions,
    scrollBehavior: ScrollBehavior,
    modifier: Modifier = Modifier,
) {
    LazyColumn(
        modifier = modifier
            .overScrollVertical()
            .fillMaxHeight()
            .nestedScroll(scrollBehavior.nestedScrollConnection)
            .imePadding(),
        contentPadding = PaddingValues(start = 12.dp, end = 12.dp, top = 8.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        item(key = "controls") {
            ControlPanel(
                state = state,
                actions = actions,
                modifier = Modifier.fillMaxWidth(),
            )
        }
        item(key = "run") {
            RunButton(
                running = state.running,
                supported = state.kernelSupported &&
                    (!state.shizukuEnabled ||
                        state.shizukuStatus == ShizukuStatus.READY),
                profileValid = state.profileInvalidPaths.isEmpty(),
                labelRes = R.string.action_run,
                onClick = actions::onRun,
                onBlockedClick = actions::onProfileInvalid,
                modifier = Modifier.fillMaxWidth(),
            )
        }
    }
}

@Composable
private fun LandscapeContent(
    state: GhostlockUiState,
    actions: GhostlockActions,
    scrollBehavior: ScrollBehavior,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier = modifier,
    ) {
        LazyColumn(
            modifier = Modifier
                .fillMaxHeight()
                .weight(1f)
                .nestedScroll(scrollBehavior.nestedScrollConnection)
                .imePadding()
                .padding(horizontal = 12.dp),
            contentPadding = PaddingValues(vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
            overscrollEffect = null,
        ) {
            item(key = "controls") {
                ControlPanel(
                    state = state,
                    actions = actions,
                    modifier = Modifier.fillMaxWidth(),
                )
            }
            item(key = "run") {
                RunButton(
                    running = state.running,
                    supported = state.kernelSupported &&
                        (!state.shizukuEnabled ||
                            state.shizukuStatus == ShizukuStatus.READY),
                    profileValid = state.profileInvalidPaths.isEmpty(),
                    labelRes = R.string.action_run,
                    onClick = actions::onRun,
                    onBlockedClick = actions::onProfileInvalid,
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        }
    }
}

@Composable
private fun ControlPanel(
    state: GhostlockUiState,
    actions: GhostlockActions,
    modifier: Modifier = Modifier,
) {
    Column(modifier = modifier) {
        ActivationStatusCard(
            supported = state.kernelSupported,
            shizukuEnabled = state.shizukuEnabled,
            shizukuStatus = state.shizukuStatus,
            showParametersHint = !state.executionHasProfile,
            onParametersClick = actions::onOpenParameters,
            onClick = actions::onStatusClick,
            modifier = Modifier.fillMaxWidth(),
        )
        DeviceInfoCard(
            deviceName = state.deviceName,
            socName = state.socName,
            kernelRelease = state.kernelRelease,
            modifier = Modifier
                .fillMaxWidth()
                .padding(top = 12.dp),
        )
        if (state.cpuPairLabels.isNotEmpty()) {
            Card(modifier = modifier.padding(top = 12.dp)) {
                OverlaySpinnerPreference(
                    title = stringResource(R.string.cpu_pair_label),
                    items = state.cpuPairLabels.map { DropdownItem(icon = null, title = it) },
                    selectedIndex = state.cpuPairIndex,
                    showValue = true,
                    onSelectedIndexChange = actions::onCpuPairSelected
                )
            }
        }
        Card(modifier = modifier.padding(top = 12.dp)) {
            SwitchPreference(
                checked = state.safeModeEnabled,
                onCheckedChange = actions::onSafeModeChanged,
                title = stringResource(R.string.safe_mode_label),
                summary = stringResource(R.string.safe_mode_summary),
            )
        }
        /* PROFILE-SUGGEST-01: the profile suggestion seeds the toggle but no
         * longer hides it; an explicit user choice overrides either way. */
        Card(modifier = modifier.padding(top = 12.dp)) {
            SwitchPreference(
                checked = state.shizukuEnabled,
                onCheckedChange = actions::onShizukuChanged,
                title = stringResource(R.string.shizuku_label),
                summary = stringResource(
                    when {
                        !state.shizukuEnabled -> R.string.shizuku_summary_off
                        state.shizukuStatus == ShizukuStatus.READY ->
                            R.string.shizuku_summary_on
                        state.shizukuStatus == ShizukuStatus.PERMISSION_REQUIRED ->
                            R.string.shizuku_status_permission_required
                        else -> R.string.shizuku_status_not_running
                    }
                ),
            )
        }
        Card(modifier = modifier.padding(top = 12.dp)) {
            ArrowPreference(
                title = stringResource(R.string.advanced_settings),
                summary = "${BuildConfig.VERSION_NAME} (${BuildConfig.VERSION_CODE}) · " +
                    BuildInfo.BUILD_TIME_LABEL,
                onClick = actions::onOpenAdvanced,
            )
        }
    }
}

@Composable
private fun ActivationStatusCard(
    supported: Boolean,
    shizukuEnabled: Boolean,
    shizukuStatus: ShizukuStatus,
    showParametersHint: Boolean,
    onParametersClick: () -> Unit,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val accessReady = !shizukuEnabled || shizukuStatus == ShizukuStatus.READY
    val active = supported && accessReady
    val cardColor = if (isSystemInDarkTheme()) {
        if (active) Color(0xFF173923) else Color(0xFF3B2715)
    } else {
        if (active) Color(0xFFDFFAE4) else Color(0xFFFFF1D6)
    }
    val statusIcon = if (active) {
        Icons.Rounded.CheckCircleOutline
    } else {
        Icons.Rounded.RemoveCircleOutline
    }
    val statusIconColor = if (active) {
        if (isSystemInDarkTheme()) Color(0xFF62D783) else Color(0xFF36D167)
    } else {
        if (isSystemInDarkTheme()) Color(0xFFFFC56C) else Color(0xFFF5A623)
    }
    Card(
        modifier = modifier.clickable(enabled = supported && shizukuEnabled && !accessReady) { onClick() },
        colors = CardDefaults.defaultColors(color = cardColor),
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(110.dp),
        ) {
            Text(
                text = stringResource(
                    when {
                        !supported -> R.string.kernel_unsupported
                        !shizukuEnabled -> R.string.kernel_supported
                        shizukuStatus == ShizukuStatus.READY -> R.string.shizuku_ready
                        shizukuStatus == ShizukuStatus.PERMISSION_REQUIRED -> R.string.shizuku_permission_required
                        else -> R.string.shizuku_not_running
                    },
                ),
                modifier = Modifier
                    .align(Alignment.TopStart)
                    .padding(start = 16.dp, top = 14.dp),
                fontSize = 22.sp,
                fontWeight = FontWeight.SemiBold,
                color = MiuixTheme.colorScheme.onSurface,
            )
            if (showParametersHint) {
                Text(
                    text = stringResource(R.string.kernel_profile_missing_hint),
                    modifier = Modifier
                        .align(Alignment.TopStart)
                        .padding(start = 16.dp, top = 52.dp)
                        .clickable(onClick = onParametersClick),
                    fontSize = 14.sp,
                    fontWeight = FontWeight.Medium,
                    color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                )
            }
            Icon(
                imageVector = statusIcon,
                contentDescription = null,
                tint = statusIconColor,
                modifier = Modifier
                    .align(Alignment.BottomEnd)
                    .offset(x = 27.dp, y = 31.dp)
                    .size(110.dp),
            )
        }
    }
}

@Composable
private fun DeviceInfoCard(
    deviceName: String,
    socName: String,
    kernelRelease: String,
    modifier: Modifier = Modifier,
) {
    Card(
        modifier = modifier,
        insideMargin = PaddingValues(16.dp),
    ) {
        SelectionContainer {
            Column(
                modifier = Modifier.fillMaxWidth(),
                verticalArrangement = Arrangement.spacedBy(20.dp),
            ) {
                DeviceInfoItem(
                    title = stringResource(R.string.device_label),
                    value = deviceName,
                )
                DeviceInfoItem(
                    title = stringResource(R.string.soc_label),
                    value = socName,
                )
                DeviceInfoItem(
                    title = stringResource(R.string.kernel_label),
                    value = kernelRelease,
                )
            }
        }
    }
}

@Composable
private fun DeviceInfoItem(
    title: String,
    value: String,
    modifier: Modifier = Modifier,
) {
    Column(modifier = modifier) {
        Text(
            text = title,
            fontSize = 18.sp,
            fontWeight = FontWeight.Medium,
            color = MiuixTheme.colorScheme.onSurface,
        )
        Text(
            text = value,
            modifier = Modifier.padding(top = 2.dp),
            fontSize = 14.sp,
            color = MiuixTheme.colorScheme.onSurface.copy(alpha = 0.68f),
        )
    }
}

@Composable
internal fun AdvancedOptions(
    state: GhostlockUiState,
    actions: GhostlockActions,
) {
    Column {
        if (state.exportVisible) {
            AdvancedAction(
                text = stringResource(R.string.action_export_offsets),
                onClick = actions::onExportOffsets,
            )
        }
    }
}

/* profile-ui: resolved execution view with auto-saved sparse overrides. */
@Composable
internal fun ExecutionEditor(
    state: GhostlockUiState,
    actions: GhostlockActions,
) {
    Card(modifier = Modifier.padding(top = 8.dp)) {
        Column(modifier = Modifier.padding(vertical = 10.dp)) {
            for (field in state.executionFields) {
                val text = state.executionEditing[field.path] ?: field.value.toString()
                val invalid = isFieldInputInvalid(text) || field.path in state.profileInvalidPaths
                TextField(
                    value = text,
                    onValueChange = { value -> actions.onExecutionFieldChanged(field.path, value) },
                    label = fieldLabel(field.path, field.path.substringAfterLast('.')),
                    colors = when {
                        invalid ->
                            TextFieldDefaults.textFieldColors(labelColor = FieldErrorHighlight)

                        field.overridden ->
                            TextFieldDefaults.textFieldColors(labelColor = OverrideHighlight)

                        else -> TextFieldDefaults.textFieldColors()
                    },
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 12.dp, vertical = 6.dp),
                    singleLine = true,
                )
            }
        }
    }
}

internal val OverrideHighlight = Color(0xFFF5A623)

/** Red marks an unfilled or non-numeric field in the parameter editors. */
internal val FieldErrorHighlight = Color(0xFFE53935)

internal fun isFieldInputInvalid(text: String): Boolean = text.trim().toLongOrNull() == null

@Composable
internal fun AdvancedAction(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    TextButton(
        text = text,
        onClick = onClick,
        modifier = modifier
            .fillMaxWidth()
            .padding(top = 12.dp),
    )
}

@Composable
private fun RunButton(
    running: Boolean,
    supported: Boolean,
    profileValid: Boolean,
    labelRes: Int,
    onClick: () -> Unit,
    onBlockedClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Box(modifier = modifier) {
        TextButton(
            text = stringResource(if (running) R.string.action_running else labelRes),
            enabled = supported && profileValid && !running,
            colors = ButtonDefaults.textButtonColorsPrimary(),
            onClick = onClick,
            modifier = Modifier.fillMaxWidth(),
        )
        /* A disabled TextButton consumes no pointer input, so this overlay
         * explains why the run is blocked. */
        if (!running && (!supported || !profileValid)) {
            Box(
                modifier = Modifier
                    .matchParentSize()
                    .clickable { onBlockedClick() },
            )
        }
    }
}

@Composable
private fun LogPanel(
    lines: List<GhostlockLogLine>,
    modifier: Modifier = Modifier,
) {
    val listState = rememberLazyListState()
    LaunchedEffect(lines.size) {
        if (lines.isNotEmpty()) listState.scrollToItem(lines.lastIndex)
    }
    Column(
        modifier = modifier
            .clip(RoundedCornerShape(8.dp))
            .background(Color(0xFF0B1220))
            .padding(12.dp),
    ) {
        SelectionContainer {
            LazyColumn(
                state = listState,
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f)
                    .padding(top = 8.dp),
            ) {
                items(lines) { line ->
                    Text(
                        text = line.text.trimEnd('\r', '\n'),
                        color = lineColor(line.color),
                        fontFamily = FontFamily.Monospace,
                        fontSize = 12.sp,
                        lineHeight = 16.sp,
                    )
                }
            }
        }
    }
}

private fun lineColor(color: Int): Color = when (color) {
    0xFFFF6B6B.toInt() -> Color(0xFFFF6B6B)
    0xFF5FD68A.toInt() -> Color(0xFF5FD68A)
    0xFFFFC94D.toInt() -> Color(0xFFFFC94D)
    0xFF60A5FA.toInt() -> Color(0xFF60A5FA)
    else -> Color(0xFFD1D5DB)
}
