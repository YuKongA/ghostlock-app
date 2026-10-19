package com.ghostlock.app.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.expandVertically
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ghostlock.app.BuildConfig
import com.ghostlock.app.R
import com.ghostlock.app.domain.model.ProfileConfig
import com.ghostlock.app.domain.model.ProfileFieldNode
import com.ghostlock.app.domain.model.UserProfileFile
import top.yukonga.miuix.kmp.basic.ButtonDefaults
import top.yukonga.miuix.kmp.basic.Card
import top.yukonga.miuix.kmp.basic.CardDefaults
import top.yukonga.miuix.kmp.basic.DropdownItem
import top.yukonga.miuix.kmp.basic.Icon
import top.yukonga.miuix.kmp.basic.IconButton
import top.yukonga.miuix.kmp.basic.MiuixScrollBehavior
import top.yukonga.miuix.kmp.basic.Scaffold
import top.yukonga.miuix.kmp.basic.SmallTitle
import top.yukonga.miuix.kmp.basic.Text
import top.yukonga.miuix.kmp.basic.TextButton
import top.yukonga.miuix.kmp.basic.TextField
import top.yukonga.miuix.kmp.basic.TextFieldDefaults
import top.yukonga.miuix.kmp.basic.TopAppBar
import top.yukonga.miuix.kmp.basic.rememberTopAppBarState
import top.yukonga.miuix.kmp.icon.MiuixIcons
import top.yukonga.miuix.kmp.icon.basic.ArrowRight
import top.yukonga.miuix.kmp.icon.basic.Check
import top.yukonga.miuix.kmp.icon.extended.Back
import top.yukonga.miuix.kmp.overlay.OverlayDialog
import top.yukonga.miuix.kmp.preference.ArrowPreference
import top.yukonga.miuix.kmp.preference.OverlaySpinnerPreference
import top.yukonga.miuix.kmp.preference.RadioButtonLocation
import top.yukonga.miuix.kmp.preference.RadioButtonPreference
import top.yukonga.miuix.kmp.preference.SwitchPreference
import top.yukonga.miuix.kmp.theme.MiuixTheme
import top.yukonga.miuix.kmp.utils.overScrollVertical
import top.yukonga.miuix.kmp.utils.scrollEndHaptic
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/* Field reference and validation rules live in the repository docs; pick the
 * page matching the system language. */
private const val ProfileDocsBase =
    "https://github.com/YuKongA/ghostlock-app/blob/main/docs/kernel_profiles/"

private fun profileDocsUrl(): String =
    ProfileDocsBase + if (Locale.getDefault().language == "zh") {
        "PROFILE_SCHEMA_ZH.md"
    } else {
        "PROFILE_SCHEMA.md"
    }

private fun Modifier.preferencePageItem(): Modifier =
    this.fillMaxWidth().padding(horizontal = 12.dp).padding(bottom = 12.dp)

private fun Modifier.profileTreeItem(depth: Int): Modifier =
    this.fillMaxWidth().padding(start = (12 + depth * 12).dp, end = 12.dp, bottom = 12.dp)

@Composable
private fun ProfileDocsCard(onOpen: () -> Unit) {
    Card {
        ArrowPreference(
            title = stringResource(R.string.profile_docs),
            summary = stringResource(R.string.profile_docs_summary),
            onClick = onOpen,
        )
    }
}

@Composable
private fun ProfileHintBanner(
    text: String,
    modifier: Modifier = Modifier,
    warning: Boolean = false,
    title: String? = null,
) {
    Card(
        modifier = modifier.fillMaxWidth(),
        insideMargin = PaddingValues(horizontal = 16.dp, vertical = 12.dp),
        colors = if (warning) {
            CardDefaults.defaultColors(
                color = MiuixTheme.colorScheme.errorContainer,
                contentColor = MiuixTheme.colorScheme.onErrorContainer,
            )
        } else {
            CardDefaults.defaultColors()
        },
    ) {
        Column(verticalArrangement = Arrangement.spacedBy(4.dp)) {
            if (title != null) {
                Text(
                    text = title,
                    fontSize = 15.sp,
                    fontWeight = FontWeight.SemiBold,
                    color = if (warning) MiuixTheme.colorScheme.onErrorContainer
                    else MiuixTheme.colorScheme.onSurface,
                )
            }
            if (text.isNotBlank()) {
                Text(
                    text = text,
                    fontSize = 13.sp,
                    color = if (warning) MiuixTheme.colorScheme.onErrorContainer
                    else MiuixTheme.colorScheme.onSurfaceVariantSummary,
                )
            }
        }
    }
}

/**
 * Advanced screen: offsets tooling, parameter overrides and the debug-only
 * log export switches. Debug preferences are hidden in release builds.
 */
@Composable
internal fun AdvancedScreen(
    state: GhostlockUiState,
    actions: GhostlockActions,
) {
    val scrollBehavior = MiuixScrollBehavior(rememberTopAppBarState())
    Scaffold(
        topBar = {
            TopAppBar(
                title = stringResource(R.string.advanced_settings),
                scrollBehavior = scrollBehavior,
                navigationIcon = {
                    IconButton(onClick = actions::onCloseAdvanced) {
                        Icon(
                            imageVector = MiuixIcons.Back,
                            contentDescription = stringResource(R.string.action_back),
                        )
                    }
                },
            )
        },
    ) { paddingValues ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .imePadding()
                .scrollEndHaptic()
                .overScrollVertical()
                .nestedScroll(scrollBehavior.nestedScrollConnection),
            contentPadding = pageContentPadding(paddingValues),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            item(key = "forceAttack") {
                Card {
                    SwitchPreference(
                        checked = state.forceAttackTestEnabled,
                        onCheckedChange = actions::onForceAttackTestChanged,
                        title = stringResource(R.string.force_attack_test_label),
                        summary = stringResource(R.string.force_attack_test_summary),
                    )
                }
            }
            item(key = "override") {
                Card {
                    ArrowPreference(
                        title = stringResource(R.string.parameters),
                        summary = stringResource(R.string.parameters_summary),
                        onClick = actions::onOpenParameters,
                    )
                }
            }
            item(key = "export") {
                Card {
                    Column {
                        SwitchPreference(
                            checked = state.debugExportEnabled,
                            onCheckedChange = actions::onDebugExportChanged,
                            title = stringResource(R.string.debug_export_log),
                            summary = stringResource(R.string.debug_export_log_summary),
                        )
                        AnimatedVisibility(
                            visible = state.debugExportEnabled,
                            enter = expandVertically(),
                            exit = shrinkVertically(),
                        ) {
                            Column {
                                ArrowPreference(
                                    title = stringResource(R.string.debug_export_location),
                                    summary = state.debugExportLocation,
                                    onClick = actions::onDebugExportLocationPick,
                                )
                                SwitchPreference(
                                    checked = state.debugKernelLogEnabled,
                                    onCheckedChange = actions::onDebugKernelLogChanged,
                                    title = stringResource(R.string.debug_kernel_log),
                                    summary = stringResource(R.string.debug_kernel_log_summary),
                                )
                            }
                        }
                        if (!BuildConfig.DEBUG) {
                            Text(
                                text = stringResource(R.string.debug_build_hint),
                                modifier = Modifier.padding(start = 16.dp, end = 16.dp, top = 8.dp, bottom = 12.dp),
                                style = MiuixTheme.textStyles.body2,
                                color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                            )
                        }
                    }
                }
            }
            item(key = "about") {
                Card {
                    ArrowPreference(
                        title = stringResource(R.string.about),
                        summary = stringResource(R.string.about_summary),
                        onClick = actions::onShowAbout,
                    )
                }
            }
        }
    }
}

/** Parameters screen: configuration loading and the parameter-override submenu. */
@Composable
internal fun ParameterScreen(
    state: GhostlockUiState,
    actions: GhostlockActions,
) {
    val scrollBehavior = MiuixScrollBehavior(rememberTopAppBarState())
    val uriHandler = LocalUriHandler.current
    Scaffold(
        topBar = {
            TopAppBar(
                title = stringResource(R.string.parameters),
                scrollBehavior = scrollBehavior,
                navigationIcon = {
                    IconButton(onClick = actions::onCloseParameters) {
                        Icon(
                            imageVector = MiuixIcons.Back,
                            contentDescription = stringResource(R.string.action_back),
                        )
                    }
                },
            )
        },
    ) { paddingValues ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .imePadding()
                .scrollEndHaptic()
                .overScrollVertical()
                .nestedScroll(scrollBehavior.nestedScrollConnection),
            contentPadding = pageContentPadding(paddingValues),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            item(key = "loaded") {
                ProfileHintBanner(
                    text = if (!state.executionHasProfile) {
                        stringResource(R.string.profile_not_loaded)
                    } else {
                        state.activeUserProfile?.let { profile ->
                            stringResource(R.string.loaded_user_profile, profile)
                        } ?: stringResource(
                            R.string.builtin_profile_label,
                            state.activeBuiltinProfile ?: stringResource(R.string.load_builtin_auto),
                        )
                    },
                )
            }
            item(key = "load") {
                Card {
                    ArrowPreference(
                        title = stringResource(R.string.load_config_title),
                        summary = stringResource(R.string.load_config_summary),
                        onClick = actions::onOpenLoadConfig,
                    )
                }
            }
            if (state.executionHasProfile) {
                item(key = "override") {
                    Card {
                        ArrowPreference(
                            title = stringResource(R.string.edit_loaded_profile),
                            summary = stringResource(R.string.override_summary),
                            onClick = actions::onOpenProfileOverrides,
                        )
                    }
                }
            }
            item(key = "docs") {
                ProfileDocsCard { uriHandler.openUri(profileDocsUrl()) }
            }
            if (state.executionHasProfile) {
                item(key = "actions") {
                    TextButton(
                        text = stringResource(R.string.override_export),
                        onClick = actions::onExportProfile,
                        modifier = Modifier.fillMaxWidth(),
                    )
                }
            }
        }
    }
}

/**
 * Configuration loading: the import/parse buttons, the builtin picker and the
 * list of verbatim user-imported documents (export, rename, delete).
 */
@Composable
internal fun LoadConfigScreen(
    state: GhostlockUiState,
    actions: GhostlockActions,
) {
    val scrollBehavior = MiuixScrollBehavior(rememberTopAppBarState())
    Scaffold(
        topBar = {
            TopAppBar(
                title = stringResource(R.string.load_config_title),
                scrollBehavior = scrollBehavior,
                navigationIcon = {
                    IconButton(onClick = actions::onCloseLoadConfig) {
                        Icon(
                            imageVector = MiuixIcons.Back,
                            contentDescription = stringResource(R.string.action_back),
                        )
                    }
                },
            )
        },
    ) { paddingValues ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .imePadding()
                .scrollEndHaptic()
                .overScrollVertical()
                .nestedScroll(scrollBehavior.nestedScrollConnection),
            contentPadding = pageContentPadding(
                paddingValues, top = 0.dp, bottom = 0.dp, horizontalMin = 0.dp,
            ),
        ) {
            item(key = "top-space") { Spacer(Modifier.height(12.dp)) }
            item(key = "import-conf") {
                Card(modifier = Modifier.preferencePageItem()) {
                    ArrowPreference(
                        title = stringResource(R.string.action_import_offsets_conf),
                        onClick = actions::onImportOffsetsHocon,
                    )
                }
            }
            item(key = "import-json") {
                Card(modifier = Modifier.preferencePageItem()) {
                    ArrowPreference(
                        title = stringResource(R.string.action_import_offsets_json),
                        onClick = actions::onImportOffsetsJson,
                    )
                }
            }
            item(key = "parse-ota") {
                Card(modifier = Modifier.preferencePageItem()) {
                    ArrowPreference(
                        title = stringResource(R.string.action_parse_ota),
                        onClick = actions::onParseOta,
                    )
                }
            }
            item(key = "parse-image") {
                Card(modifier = Modifier.preferencePageItem()) {
                    ArrowPreference(
                        title = stringResource(R.string.action_parse),
                        onClick = actions::onParseImage,
                    )
                }
            }
            item(key = "user-title") {
                SmallTitle(text = stringResource(R.string.user_profiles_title))
            }
            item(key = "builtin") {
                Card(modifier = Modifier.preferencePageItem()) {
                    ArrowPreference(
                        title = stringResource(R.string.load_builtin_profile),
                        summary = stringResource(R.string.load_builtin_summary),
                        onClick = actions::onOpenBuiltinProfiles,
                        endActions = {
                            if (state.executionHasProfile && state.activeUserProfile == null) {
                                Text(
                                    text = stringResource(R.string.user_profile_loaded_badge),
                                    fontSize = 14.sp,
                                    fontWeight = FontWeight.SemiBold,
                                    color = OverrideHighlight,
                                )
                            }
                        },
                    )
                }
            }
            if (state.userProfiles.isEmpty()) {
                item(key = "user-empty") {
                    ProfileHintBanner(
                        text = stringResource(R.string.user_profiles_empty),
                        modifier = Modifier.preferencePageItem(),
                    )
                }
            }
            items(state.userProfiles, key = { it.name }) { profile ->
                UserProfileCard(
                    profile = profile,
                    loaded = state.executionHasProfile && profile.name == state.activeUserProfile,
                    onClick = { actions.onOpenUserProfileDetail(profile.name) },
                    modifier = Modifier.preferencePageItem(),
                )
            }
            item(key = "bottom-space") {
                Spacer(Modifier.height(24.dp).navigationBarsPadding())
            }
        }
    }
}

/** One stored document; taps open the secondary menu with its actions. */
@Composable
private fun UserProfileCard(
    profile: UserProfileFile,
    loaded: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Card(modifier = modifier.fillMaxWidth()) {
        val status = if (profile.parseError) {
            stringResource(R.string.user_profile_parse_error)
        } else {
            releaseSummary(profile.releases)
        }
        val metadata = stringResource(
            R.string.user_profile_meta,
            formatFileSize(profile.sizeBytes),
            formatProfileTime(profile.importedAt),
        )
        ArrowPreference(
            title = profile.name,
            summary = "$status\n$metadata",
            onClick = onClick,
            endActions = {
                Text(
                    text = "v${profile.version}",
                    fontSize = 13.sp,
                    fontWeight = FontWeight.SemiBold,
                    color = if (profile.version == 1) FieldErrorHighlight
                    else MiuixTheme.colorScheme.onSurfaceVariantSummary,
                )
                if (loaded) {
                    Icon(
                        imageVector = MiuixIcons.Basic.Check,
                        contentDescription = stringResource(R.string.user_profile_loaded_badge),
                        tint = OverrideHighlight,
                        modifier = Modifier
                            .padding(start = 8.dp)
                            .size(18.dp),
                    )
                }
            },
        )
    }
}

/**
 * Secondary menu for one stored document: load/unload, modify (the shared
 * parameter-override editor), export, rename and delete.
 */
@Composable
internal fun UserProfileDetailScreen(
    state: GhostlockUiState,
    actions: GhostlockActions,
    name: String,
) {
    val scrollBehavior = MiuixScrollBehavior(rememberTopAppBarState())
    val profile = state.userProfiles.firstOrNull { it.name == name }
    val loaded = state.executionHasProfile && name == state.activeUserProfile
    Scaffold(
        topBar = {
            TopAppBar(
                title = stringResource(R.string.user_profile_detail),
                scrollBehavior = scrollBehavior,
                navigationIcon = {
                    IconButton(onClick = actions::onCloseUserProfileDetail) {
                        Icon(
                            imageVector = MiuixIcons.Back,
                            contentDescription = stringResource(R.string.action_back),
                        )
                    }
                },
            )
        },
    ) { paddingValues ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .imePadding()
                .scrollEndHaptic()
                .overScrollVertical()
                .nestedScroll(scrollBehavior.nestedScrollConnection),
            contentPadding = pageContentPadding(paddingValues),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            item(key = "info") {
                val details = if (profile == null) "" else buildList {
                    add(
                        if (profile.parseError) stringResource(R.string.user_profile_parse_error)
                        else releaseSummary(profile.releases),
                    )
                    add(
                        stringResource(
                            R.string.user_profile_meta,
                            formatFileSize(profile.sizeBytes),
                            formatProfileTime(profile.importedAt),
                        ),
                    )
                    add(stringResource(R.string.user_profile_version, profile.version))
                    if (loaded) add(stringResource(R.string.user_profile_loaded_badge))
                }.joinToString("\n")
                ProfileHintBanner(
                    title = profile?.name ?: name,
                    text = details,
                    warning = profile?.parseError == true,
                )
            }
            if (profile != null) {
                item(key = "load") {
                    TextButton(
                        text = stringResource(
                            if (loaded) R.string.user_profile_unload
                            else R.string.user_profile_load,
                        ),
                        onClick = {
                            if (loaded) actions.onUnloadUserProfile()
                            else actions.onLoadUserProfile(profile.name)
                        },
                        colors = if (loaded) {
                            ButtonDefaults.textButtonColors()
                        } else {
                            ButtonDefaults.textButtonColorsPrimary()
                        },
                        modifier = Modifier.fillMaxWidth(),
                    )
                }
                if (profile.version == 1) {
                    item(key = "legacy-notice") {
                        ProfileHintBanner(text = stringResource(R.string.user_profile_legacy_notice))
                    }
                    item(key = "convert") {
                        TextButton(
                            text = stringResource(R.string.user_profile_convert),
                            onClick = { actions.onConvertUserProfile(profile.name) },
                            colors = ButtonDefaults.textButtonColorsPrimary(),
                            modifier = Modifier.fillMaxWidth(),
                        )
                    }
                } else {
                    item(key = "edit") {
                        Card {
                            ArrowPreference(
                                title = stringResource(R.string.user_profile_edit),
                                onClick = { actions.onEditUserProfile(profile.name) },
                            )
                        }
                    }
                }
                item(key = "export") {
                    TextButton(
                        text = stringResource(R.string.user_profile_export),
                        onClick = { actions.onUserProfileExport(profile.name) },
                        modifier = Modifier.fillMaxWidth(),
                    )
                }
                item(key = "rename") {
                    TextButton(
                        text = stringResource(R.string.user_profile_rename),
                        onClick = { actions.onUserProfileRename(profile.name) },
                        modifier = Modifier.fillMaxWidth(),
                    )
                }
                item(key = "delete") {
                    TextButton(
                        text = stringResource(R.string.user_profile_delete),
                        onClick = { actions.onUserProfileDelete(profile.name) },
                        modifier = Modifier.fillMaxWidth(),
                    )
                }
            }
        }
    }
    val deleteTarget = state.userProfileDeleteTarget
    OverlayDialog(
        show = deleteTarget != null,
        title = stringResource(R.string.user_profile_delete_title),
        summary = deleteTarget?.let { stringResource(R.string.user_profile_delete_message, it) },
        onDismissRequest = actions::onUserProfileDeleteDismiss,
        content = {
            Row(modifier = Modifier.fillMaxWidth()) {
                TextButton(
                    modifier = Modifier.weight(1f),
                    text = stringResource(R.string.cancel),
                    onClick = actions::onUserProfileDeleteDismiss,
                )
                Spacer(modifier = Modifier.width(12.dp))
                TextButton(
                    modifier = Modifier.weight(1f),
                    text = stringResource(R.string.user_profile_delete),
                    colors = ButtonDefaults.textButtonColorsPrimary(),
                    onClick = actions::onUserProfileDeleteConfirm,
                )
            }
        },
    )
}

private fun releaseSummary(releases: List<String>): String {
    if (releases.isEmpty()) return ""
    val head = releases.take(3).joinToString(", ")
    return if (releases.size > 3) "$head +${releases.size - 3}" else head
}

private fun formatFileSize(bytes: Long): String = when {
    bytes >= 1024 * 1024 -> String.format(Locale.US, "%.1f MB", bytes / (1024.0 * 1024.0))
    bytes >= 1024 -> String.format(Locale.US, "%.1f KB", bytes / 1024.0)
    else -> "$bytes B"
}

private fun formatProfileTime(millis: Long): String =
    SimpleDateFormat("yyyy-MM-dd HH:mm", Locale.getDefault()).format(Date(millis))

/** Parameter overrides: general editor, its reset and the advanced submenu. */
@Composable
internal fun ProfileOverrideScreen(
    state: GhostlockUiState,
    actions: GhostlockActions,
) {
    val scrollBehavior = MiuixScrollBehavior(rememberTopAppBarState())
    Scaffold(
        topBar = {
            TopAppBar(
                title = stringResource(R.string.override_title),
                scrollBehavior = scrollBehavior,
                navigationIcon = {
                    IconButton(onClick = actions::onCloseProfileOverrides) {
                        Icon(
                            imageVector = MiuixIcons.Back,
                            contentDescription = stringResource(R.string.action_back),
                        )
                    }
                },
            )
        },
    ) { paddingValues ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .imePadding()
                .scrollEndHaptic()
                .overScrollVertical()
                .nestedScroll(scrollBehavior.nestedScrollConnection),
            contentPadding = pageContentPadding(
                paddingValues, top = 0.dp, bottom = 0.dp, horizontalMin = 0.dp,
            ),
        ) {
            item(key = "top-space") { Spacer(Modifier.height(12.dp)) }
            item(key = "general-banner") {
                val source = buildList {
                    add(
                        stringResource(
                            R.string.loaded_profile,
                            state.activeBuiltinProfile ?: state.executionRelease,
                        ),
                    )
                    if (state.activeBuiltinProfile != null) {
                        add(stringResource(R.string.target_kernel, state.executionRelease))
                    }
                    state.activeUserProfile?.let {
                        add(stringResource(R.string.loaded_user_profile, it))
                    }
                    state.editTargetName?.let {
                        add(stringResource(R.string.editing_profile, it))
                    }
                }.joinToString("\n")
                ProfileHintBanner(
                    text = source,
                    warning = state.activeBuiltinProfile != null,
                    modifier = Modifier.preferencePageItem(),
                )
            }
            if (state.executionHasProfile) {
                item(key = "fields") {
                    ExecutionEditor(
                        state = state,
                        actions = actions,
                        modifier = Modifier.preferencePageItem(),
                    )
                }
            }
            item(key = "advanced") {
                Card(modifier = Modifier.preferencePageItem()) {
                    ArrowPreference(
                        title = stringResource(R.string.debug_profile_override),
                        summary = stringResource(R.string.debug_profile_override_summary),
                        onClick = actions::onOpenAdvancedOverrides,
                    )
                }
            }
            item(key = "actions") {
                Column(
                    modifier = Modifier.preferencePageItem(),
                    verticalArrangement = Arrangement.spacedBy(12.dp),
                ) {
                    if (state.editTargetName == null) {
                        ProfileHintBanner(
                            text = stringResource(R.string.builtin_edit_save_hint),
                            warning = true,
                        )
                    }
                    TextButton(
                        text = stringResource(R.string.profile_save),
                        enabled = state.editTargetName != null,
                        onClick = actions::onSaveProfileEdits,
                        colors = ButtonDefaults.textButtonColorsPrimary(),
                        modifier = Modifier.fillMaxWidth(),
                    )
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(12.dp),
                    ) {
                        TextButton(
                            text = stringResource(R.string.profile_save_as),
                            onClick = actions::onSaveProfileAs,
                            modifier = Modifier.weight(1f),
                        )
                        TextButton(
                            text = stringResource(R.string.override_export),
                            onClick = actions::onExportProfileEdits,
                            modifier = Modifier.weight(1f),
                        )
                    }
                    TextButton(
                        text = stringResource(R.string.profile_revert),
                        onClick = actions::onRevertProfileEdits,
                        modifier = Modifier.fillMaxWidth(),
                    )
                }
            }
            item(key = "bottom-space") {
                Spacer(Modifier.height(24.dp).navigationBarsPadding())
            }
        }
    }
}

/**
 * Builtin picker: the device kernel stays pinned on top, every bundled
 * release follows, ordered by how close it is to the device kernel.
 */
@Composable
internal fun BuiltinProfileScreen(
    state: GhostlockUiState,
    actions: GhostlockActions,
) {
    val scrollBehavior = MiuixScrollBehavior(rememberTopAppBarState())
    Scaffold(
        topBar = {
            TopAppBar(
                title = stringResource(R.string.load_builtin_profile),
                scrollBehavior = scrollBehavior,
                navigationIcon = {
                    IconButton(onClick = actions::onCloseBuiltinProfiles) {
                        Icon(
                            imageVector = MiuixIcons.Back,
                            contentDescription = stringResource(R.string.action_back),
                        )
                    }
                },
            )
        },
    ) { paddingValues ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .scrollEndHaptic()
                .overScrollVertical()
                .nestedScroll(scrollBehavior.nestedScrollConnection),
            contentPadding = pageContentPadding(
                paddingValues, top = 0.dp, bottom = 0.dp, horizontalMin = 0.dp,
            ),
        ) {
            item(key = "top-space") { Spacer(Modifier.height(12.dp)) }
            item(key = "local-kernel") {
                ProfileHintBanner(
                    text = stringResource(R.string.local_kernel, state.kernelRelease),
                    modifier = Modifier.preferencePageItem(),
                )
            }
            if (state.activeBuiltinProfile == null && state.activeUserProfile == null &&
                !state.executionHasProfile
            ) {
                item(key = "auto-no-match") {
                    ProfileHintBanner(
                        text = stringResource(R.string.builtin_auto_no_match),
                        warning = true,
                        modifier = Modifier.preferencePageItem(),
                    )
                }
            }
            item(key = "immutable-notice") {
                ProfileHintBanner(
                    text = stringResource(R.string.builtin_immutable_notice),
                    modifier = Modifier.preferencePageItem(),
                )
            }
            item(key = "auto") {
                BuiltinProfileRow(
                    title = stringResource(R.string.load_builtin_auto),
                    selected = state.activeUserProfile == null &&
                            state.activeBuiltinProfile == null,
                    onClick = { actions.onSelectBuiltinProfile(null) },
                    modifier = Modifier.preferencePageItem(),
                )
            }
            if (state.builtinTemplates.isNotEmpty()) {
                item(key = "templates-title") {
                    SmallTitle(text = stringResource(R.string.templates_section))
                }
                items(state.builtinTemplates, key = { "template:$it" }) { release ->
                    BuiltinProfileRow(
                        title = release,
                        selected = state.activeUserProfile == null &&
                                state.activeBuiltinProfile == release,
                        onClick = { actions.onSelectBuiltinProfile(release) },
                        modifier = Modifier.preferencePageItem(),
                    )
                }
            }
            if (state.builtinProfiles.isNotEmpty()) {
                item(key = "kernels-title") {
                    SmallTitle(text = stringResource(R.string.kernels_section))
                }
                items(state.builtinProfiles, key = { "kernel:$it" }) { release ->
                    BuiltinProfileRow(
                        title = release,
                        selected = state.activeUserProfile == null &&
                                state.activeBuiltinProfile == release,
                        onClick = { actions.onSelectBuiltinProfile(release) },
                        modifier = Modifier.preferencePageItem(),
                    )
                }
            }
            item(key = "bottom-space") {
                Spacer(Modifier.height(24.dp).navigationBarsPadding())
            }
        }
    }
}

@Composable
private fun BuiltinProfileRow(
    title: String,
    selected: Boolean,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Card(modifier = modifier.fillMaxWidth()) {
        RadioButtonPreference(
            title = title,
            selected = selected,
            onClick = onClick,
            radioButtonLocation = RadioButtonLocation.End,
        )
    }
}

/** Tree editor for every numeric leaf of the resolved profile. */
@Composable
internal fun AdvancedOverrideScreen(
    state: GhostlockUiState,
    actions: GhostlockActions,
) {
    val scrollBehavior = MiuixScrollBehavior(rememberTopAppBarState())
    val expanded = remember { mutableStateMapOf<String, Boolean>() }
    Scaffold(
        topBar = {
            TopAppBar(
                title = stringResource(R.string.debug_profile_override),
                scrollBehavior = scrollBehavior,
                navigationIcon = {
                    IconButton(onClick = actions::onCloseAdvancedOverrides) {
                        Icon(
                            imageVector = MiuixIcons.Back,
                            contentDescription = stringResource(R.string.action_back),
                        )
                    }
                },
            )
        },
    ) { paddingValues ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .imePadding()
                .scrollEndHaptic()
                .overScrollVertical()
                .nestedScroll(scrollBehavior.nestedScrollConnection),
            contentPadding = pageContentPadding(
                paddingValues, top = 0.dp, bottom = 0.dp, horizontalMin = 0.dp,
            ),
        ) {
            item(key = "top-space") { Spacer(Modifier.height(12.dp)) }
            item(key = "warning") {
                ProfileHintBanner(
                    text = stringResource(R.string.debug_profile_override_warning),
                    warning = true,
                    modifier = Modifier.preferencePageItem(),
                )
            }
            item(key = "route") {
                Card(modifier = Modifier.preferencePageItem()) {
                    Column {
                        OverlaySpinnerPreference(
                            title = stringResource(R.string.route_label),
                            items = (listOf(stringResource(R.string.route_auto)) + ProfileConfig.Routes)
                                .map { DropdownItem(icon = null, title = it) },
                            selectedIndex = ProfileConfig.Routes.indexOf(state.profileRoute) + 1,
                            showValue = true,
                            onSelectedIndexChange = actions::onRouteChanged,
                        )
                        OverlaySpinnerPreference(
                            title = stringResource(R.string.fallback_label),
                            items = (listOf(stringResource(R.string.fallback_none)) + ProfileConfig.Routes)
                                .map { DropdownItem(icon = null, title = it) },
                            selectedIndex = ProfileConfig.Routes.indexOf(state.profileFallback) + 1,
                            showValue = true,
                            onSelectedIndexChange = actions::onFallbackChanged,
                        )
                    }
                }
            }
            item(key = "release") {
                val source = buildList {
                    add(
                        stringResource(
                            R.string.loaded_profile,
                            state.activeBuiltinProfile ?: state.profileOverrideRelease,
                        ),
                    )
                    if (state.activeBuiltinProfile != null) {
                        add(stringResource(R.string.target_kernel, state.profileOverrideRelease))
                    }
                    state.activeUserProfile?.let {
                        add(stringResource(R.string.loaded_user_profile, it))
                    }
                }.joinToString("\n")
                ProfileHintBanner(
                    text = source,
                    warning = state.activeBuiltinProfile != null,
                    modifier = Modifier.preferencePageItem(),
                )
            }
            item(key = "tree") {
                Column {
                    ProfileTree(
                        nodes = state.profileOverrideRoots,
                        depth = 0,
                        editing = state.profileOverrideEditing,
                        invalidPaths = state.profileInvalidPaths,
                        expanded = expanded,
                        onToggle = { path, value -> expanded[path] = value },
                        onValueChange = actions::onProfileOverrideChanged,
                    )
                }
            }
            item(key = "actions") {
                TextButton(
                    text = stringResource(R.string.profile_revert),
                    onClick = actions::onRevertProfileEdits,
                    modifier = Modifier.preferencePageItem(),
                )
            }
            item(key = "bottom-space") {
                Spacer(Modifier.height(24.dp).navigationBarsPadding())
            }
        }
    }
}

/** Parent/child tree with collapsible groups and highlighted overrides. */
@Composable
private fun ProfileTree(
    nodes: List<ProfileFieldNode>,
    depth: Int,
    editing: Map<String, String>,
    invalidPaths: Set<String>,
    expanded: MutableMap<String, Boolean>,
    onToggle: (String, Boolean) -> Unit,
    onValueChange: (String, String) -> Unit,
) {
    nodes.forEach { node ->
        if (node.isGroup) {
            val isExpanded = expanded[node.path] ?: false
            val arrowRotation by animateFloatAsState(
                targetValue = if (isExpanded) 90f else 0f,
                label = "group-arrow",
            )
            Column {
                Card(
                    modifier = Modifier.profileTreeItem(depth),
                    showIndication = true,
                    onClick = { onToggle(node.path, !isExpanded) },
                ) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 16.dp, vertical = 12.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Text(
                            text = node.name,
                            modifier = Modifier.weight(1f),
                            fontSize = if (depth == 0) 17.sp else 15.sp,
                            fontWeight = if (node.overridden) FontWeight.Bold
                            else FontWeight.SemiBold,
                            color = if (node.overridden) OverrideHighlight
                            else MiuixTheme.colorScheme.onSurface,
                        )
                        Icon(
                            imageVector = MiuixIcons.Basic.ArrowRight,
                            contentDescription = null,
                            tint = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                            modifier = Modifier
                                .size(18.dp)
                                .rotate(arrowRotation),
                        )
                    }
                }
                AnimatedVisibility(
                    visible = isExpanded,
                    enter = expandVertically(),
                    exit = shrinkVertically(),
                ) {
                    Column {
                        ProfileTree(
                            nodes = node.children,
                            depth = depth + 1,
                            editing = editing,
                            invalidPaths = invalidPaths,
                            expanded = expanded,
                            onToggle = onToggle,
                            onValueChange = onValueChange,
                        )
                    }
                }
            }
        } else {
            val text = editing[node.path].orEmpty()
            val invalid = isFieldInputInvalid(text) || node.path in invalidPaths
            TextField(
                value = text,
                onValueChange = { value -> onValueChange(node.path, value) },
                label = fieldLabel(node.path, node.name),
                colors = when {
                    invalid ->
                        TextFieldDefaults.textFieldColors(labelColor = FieldErrorHighlight)

                    node.overridden ->
                        TextFieldDefaults.textFieldColors(labelColor = OverrideHighlight)

                    else -> TextFieldDefaults.textFieldColors()
                },
                modifier = Modifier.profileTreeItem(depth),
                singleLine = true,
            )
        }
    }
}
