package com.ghostlock.app.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.expandVertically
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.clickable
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.ErrorOutline
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ghostlock.app.BuildConfig
import com.ghostlock.app.BuildInfo
import com.ghostlock.app.R
import com.ghostlock.app.domain.model.ProfileConfig
import com.ghostlock.app.domain.model.ProfileFieldNode
import com.ghostlock.app.domain.model.UserProfileFile
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import top.yukonga.miuix.kmp.basic.*
import top.yukonga.miuix.kmp.icon.MiuixIcons
import top.yukonga.miuix.kmp.icon.basic.ArrowRight
import top.yukonga.miuix.kmp.icon.basic.Check
import top.yukonga.miuix.kmp.icon.extended.Back
import top.yukonga.miuix.kmp.overlay.OverlayDialog
import top.yukonga.miuix.kmp.preference.ArrowPreference
import top.yukonga.miuix.kmp.preference.SwitchPreference
import top.yukonga.miuix.kmp.theme.MiuixTheme

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
                .padding(paddingValues),
            contentPadding = PaddingValues(start = 12.dp, end = 12.dp, top = 8.dp, bottom = 12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
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
                        /* Save location and the kernel-log switch are
                         * sub-options of the export toggle, so they reveal
                         * indented inside the same card. */
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
                                    modifier = Modifier.padding(start = 16.dp),
                                )
                                SwitchPreference(
                                    checked = state.debugKernelLogEnabled,
                                    onCheckedChange = actions::onDebugKernelLogChanged,
                                    title = stringResource(R.string.debug_kernel_log),
                                    summary = stringResource(R.string.debug_kernel_log_summary),
                                    modifier = Modifier.padding(start = 16.dp),
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
                        onClick = actions::onShowAbout,
                    )
                }
            }
            item(key = "build") {
                Text(
                    text = "${BuildConfig.VERSION_NAME} (${BuildConfig.VERSION_CODE}) · " +
                        BuildInfo.BUILD_TIME_LABEL,
                    modifier = Modifier.padding(horizontal = 4.dp, vertical = 4.dp),
                    fontSize = 13.sp,
                    color = MiuixTheme.colorScheme.onSurface.copy(alpha = 0.68f),
                )
            }
        }
        GhostlockAboutDialog(
            show = state.aboutVisible,
            onDismissRequest = actions::onCloseAbout,
        )
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
                .padding(paddingValues),
            contentPadding = PaddingValues(start = 12.dp, end = 12.dp, top = 8.dp, bottom = 12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            item(key = "loaded") {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp)) {
                        /* The controller loads a single source: a user document
                         * when one is loaded, the builtin otherwise. */
                        Text(
                            text = state.activeUserProfile?.let { profile ->
                                stringResource(R.string.loaded_user_profile, profile)
                            } ?: stringResource(
                                R.string.builtin_profile_label,
                                state.activeBuiltinProfile
                                    ?: stringResource(R.string.load_builtin_auto),
                            ),
                            style = MiuixTheme.textStyles.body2,
                            color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                        )
                    }
                }
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
            item(key = "override") {
                Card {
                    ArrowPreference(
                        title = stringResource(R.string.edit_loaded_profile),
                        summary = stringResource(R.string.override_summary),
                        onClick = actions::onOpenProfileOverrides,
                    )
                }
            }
            item(key = "docs") {
                ProfileDocsCard { uriHandler.openUri(profileDocsUrl()) }
            }
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
                .padding(paddingValues),
            contentPadding = PaddingValues(start = 12.dp, end = 12.dp, top = 8.dp, bottom = 12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            item(key = "tools") {
                Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    TextButton(
                        text = stringResource(R.string.action_import_offsets_conf),
                        onClick = actions::onImportOffsetsHocon,
                        colors = ButtonDefaults.textButtonColorsPrimary(),
                        modifier = Modifier.fillMaxWidth(),
                    )
                    TextButton(
                        text = stringResource(R.string.action_import_offsets_json),
                        onClick = actions::onImportOffsetsJson,
                        colors = ButtonDefaults.textButtonColorsPrimary(),
                        modifier = Modifier.fillMaxWidth(),
                    )
                    TextButton(
                        text = stringResource(R.string.action_parse_ota),
                        onClick = actions::onParseOta,
                        colors = ButtonDefaults.textButtonColorsPrimary(),
                        modifier = Modifier.fillMaxWidth(),
                    )
                    TextButton(
                        text = stringResource(R.string.action_parse),
                        onClick = actions::onParseImage,
                        colors = ButtonDefaults.textButtonColorsPrimary(),
                        modifier = Modifier.fillMaxWidth(),
                    )
                }
            }
            item(key = "user-title") {
                Text(
                    text = stringResource(R.string.user_profiles_title),
                    modifier = Modifier.padding(start = 4.dp, top = 4.dp),
                    fontSize = 17.sp,
                    fontWeight = FontWeight.SemiBold,
                    color = MiuixTheme.colorScheme.onSurface,
                )
            }
            item(key = "builtin") {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable { actions.onOpenBuiltinProfiles() }
                            .padding(horizontal = 16.dp, vertical = 14.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text(
                                text = stringResource(R.string.load_builtin_profile),
                                fontSize = 15.sp,
                                fontWeight = FontWeight.SemiBold,
                                color = MiuixTheme.colorScheme.onSurface,
                            )
                            Text(
                                text = stringResource(R.string.load_builtin_summary),
                                modifier = Modifier.padding(top = 4.dp),
                                style = MiuixTheme.textStyles.body2,
                                color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                            )
                        }
                        /* The builtin is the loaded source only while no user
                         * document is loaded. */
                        if (state.activeUserProfile == null) {
                            Text(
                                text = stringResource(R.string.user_profile_loaded_badge),
                                modifier = Modifier.padding(start = 8.dp),
                                fontSize = 14.sp,
                                fontWeight = FontWeight.SemiBold,
                                color = OverrideHighlight,
                            )
                        }
                        Icon(
                            imageVector = MiuixIcons.Basic.ArrowRight,
                            contentDescription = null,
                            tint = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                            modifier = Modifier
                                .padding(start = 8.dp)
                                .size(18.dp),
                        )
                    }
                }
            }
            if (state.userProfiles.isEmpty()) {
                item(key = "user-empty") {
                    Card(modifier = Modifier.fillMaxWidth()) {
                        Text(
                            text = stringResource(R.string.user_profiles_empty),
                            modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp),
                            style = MiuixTheme.textStyles.body2,
                            color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                        )
                    }
                }
            }
            items(state.userProfiles, key = { it.name }) { profile ->
                UserProfileCard(
                    profile = profile,
                    loaded = profile.name == state.activeUserProfile,
                    onClick = { actions.onOpenUserProfileDetail(profile.name) },
                )
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
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clickable(onClick = onClick)
                .padding(horizontal = 16.dp, vertical = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = profile.name,
                    fontSize = 15.sp,
                    fontWeight = FontWeight.SemiBold,
                    color = MiuixTheme.colorScheme.onSurface,
                )
                Text(
                    text = if (profile.parseError) {
                        stringResource(R.string.user_profile_parse_error)
                    } else {
                        releaseSummary(profile.releases)
                    },
                    modifier = Modifier.padding(top = 4.dp),
                    style = MiuixTheme.textStyles.body2,
                    color = if (profile.parseError) FieldErrorHighlight
                    else MiuixTheme.colorScheme.onSurfaceVariantSummary,
                )
                Text(
                    text = stringResource(
                        R.string.user_profile_meta,
                        formatFileSize(profile.sizeBytes),
                        formatProfileTime(profile.importedAt),
                    ),
                    modifier = Modifier.padding(top = 2.dp),
                    style = MiuixTheme.textStyles.body2,
                    color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                )
            }
            Text(
                text = "v${profile.version}",
                modifier = Modifier.padding(start = 8.dp),
                fontSize = 13.sp,
                fontWeight = FontWeight.SemiBold,
                color = if (profile.version == 1) {
                    FieldErrorHighlight
                } else {
                    MiuixTheme.colorScheme.onSurfaceVariantSummary
                },
            )
            if (loaded) {
                Text(
                    text = stringResource(R.string.user_profile_loaded_badge),
                    modifier = Modifier.padding(start = 8.dp),
                    fontSize = 14.sp,
                    fontWeight = FontWeight.SemiBold,
                    color = OverrideHighlight,
                )
            }
            Icon(
                imageVector = MiuixIcons.Basic.ArrowRight,
                contentDescription = null,
                tint = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                modifier = Modifier
                    .padding(start = 8.dp)
                    .size(18.dp),
            )
        }
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
) {
    val scrollBehavior = MiuixScrollBehavior(rememberTopAppBarState())
    val name = state.userProfileDetail
    val profile = state.userProfiles.firstOrNull { it.name == name }
    val loaded = name != null && name == state.activeUserProfile
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
                .padding(paddingValues),
            contentPadding = PaddingValues(start = 12.dp, end = 12.dp, top = 8.dp, bottom = 12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            item(key = "info") {
                Card(modifier = Modifier.fillMaxWidth()) {
                    Column(modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp)) {
                        Text(
                            text = profile?.name ?: name.orEmpty(),
                            fontSize = 16.sp,
                            fontWeight = FontWeight.SemiBold,
                            color = MiuixTheme.colorScheme.onSurface,
                        )
                        if (profile != null) {
                            Text(
                                text = if (profile.parseError) {
                                    stringResource(R.string.user_profile_parse_error)
                                } else {
                                    releaseSummary(profile.releases)
                                },
                                modifier = Modifier.padding(top = 6.dp),
                                style = MiuixTheme.textStyles.body2,
                                color = if (profile.parseError) FieldErrorHighlight
                                else MiuixTheme.colorScheme.onSurfaceVariantSummary,
                            )
                            Text(
                                text = stringResource(
                                    R.string.user_profile_meta,
                                    formatFileSize(profile.sizeBytes),
                                    formatProfileTime(profile.importedAt),
                                ),
                                modifier = Modifier.padding(top = 4.dp),
                                style = MiuixTheme.textStyles.body2,
                                color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                            )
                            Text(
                                text = stringResource(R.string.user_profile_version, profile.version),
                                modifier = Modifier.padding(top = 4.dp),
                                style = MiuixTheme.textStyles.body2,
                                color = if (profile.version == 1) {
                                    FieldErrorHighlight
                                } else {
                                    MiuixTheme.colorScheme.onSurfaceVariantSummary
                                },
                            )
                            if (loaded) {
                                Text(
                                    text = stringResource(R.string.user_profile_loaded_badge),
                                    modifier = Modifier.padding(top = 6.dp),
                                    fontSize = 14.sp,
                                    fontWeight = FontWeight.SemiBold,
                                    color = OverrideHighlight,
                                )
                            }
                        }
                    }
                }
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
                        Card(modifier = Modifier.fillMaxWidth()) {
                            Text(
                                text = stringResource(R.string.user_profile_legacy_notice),
                                modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp),
                                style = MiuixTheme.textStyles.body2,
                                color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                            )
                        }
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
                        TextButton(
                            text = stringResource(R.string.user_profile_edit),
                            onClick = { actions.onEditUserProfile(profile.name) },
                            colors = ButtonDefaults.textButtonColorsPrimary(),
                            modifier = Modifier.fillMaxWidth(),
                        )
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
    val uriHandler = LocalUriHandler.current
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
                .padding(paddingValues),
            contentPadding = PaddingValues(start = 12.dp, end = 12.dp, top = 8.dp, bottom = 12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            item(key = "general") {
                Column {
                    Text(
                        text = stringResource(R.string.override_general),
                        modifier = Modifier.padding(start = 4.dp, top = 4.dp),
                        fontSize = 17.sp,
                        fontWeight = FontWeight.SemiBold,
                        color = MiuixTheme.colorScheme.onSurface,
                    )
                    Card(modifier = Modifier.padding(top = 8.dp)) {
                        Column(modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp)) {
                            Text(
                                text = stringResource(
                                    R.string.loaded_profile,
                                    state.activeBuiltinProfile
                                        ?: state.executionRelease,
                                ),
                                style = MiuixTheme.textStyles.body2,
                                color = if (state.activeBuiltinProfile != null) {
                                    FieldErrorHighlight
                                } else {
                                    MiuixTheme.colorScheme.onSurfaceVariantSummary
                                },
                            )
                            if (state.activeBuiltinProfile != null) {
                                Text(
                                    text = stringResource(
                                        R.string.target_kernel,
                                        state.executionRelease,
                                    ),
                                    modifier = Modifier.padding(top = 4.dp),
                                    style = MiuixTheme.textStyles.body2,
                                    color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                                )
                            }
                            state.activeUserProfile?.let { userProfile ->
                                Text(
                                    text = stringResource(R.string.loaded_user_profile, userProfile),
                                    modifier = Modifier.padding(top = 4.dp),
                                    style = MiuixTheme.textStyles.body2,
                                    color = OverrideHighlight,
                                )
                            }
                            state.editTargetName?.let { target ->
                                Text(
                                    text = stringResource(R.string.editing_profile, target),
                                    modifier = Modifier.padding(top = 4.dp),
                                    style = MiuixTheme.textStyles.body2,
                                    color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                                )
                            }
                        }
                    }
                    if (state.executionHasProfile) {
                        ExecutionEditor(state = state, actions = actions)
                    }
                }
            }
            item(key = "advanced") {
                Card {
                    ArrowPreference(
                        title = stringResource(R.string.debug_profile_override),
                        summary = stringResource(R.string.debug_profile_override_summary),
                        onClick = actions::onOpenAdvancedOverrides,
                    )
                }
            }
            item(key = "docs") {
                ProfileDocsCard { uriHandler.openUri(profileDocsUrl()) }
            }
            item(key = "actions") {
                Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    if (state.editTargetName == null) {
                        Text(
                            text = stringResource(R.string.builtin_edit_save_hint),
                            style = MiuixTheme.textStyles.body2,
                            color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
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
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(paddingValues),
        ) {
            Card(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(start = 12.dp, end = 12.dp, top = 8.dp, bottom = 4.dp),
            ) {
                Text(
                    text = stringResource(R.string.local_kernel, state.kernelRelease),
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp),
                    fontSize = 15.sp,
                    fontWeight = FontWeight.SemiBold,
                    color = MiuixTheme.colorScheme.onSurface,
                )
            }
            Card(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(start = 12.dp, end = 12.dp, top = 4.dp, bottom = 4.dp),
            ) {
                Text(
                    text = stringResource(R.string.builtin_immutable_notice),
                    modifier = Modifier.padding(horizontal = 16.dp, vertical = 14.dp),
                    style = MiuixTheme.textStyles.body2,
                    color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                )
            }
            LazyColumn(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f),
                contentPadding = PaddingValues(start = 12.dp, end = 12.dp, top = 4.dp, bottom = 12.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                item(key = "auto") {
                    BuiltinProfileRow(
                        title = stringResource(R.string.load_builtin_auto),
                        /* A loaded user document shadows the builtin choice, so
                         * no row is highlighted while one is active. */
                        selected = state.activeUserProfile == null &&
                            state.activeBuiltinProfile == null,
                        onClick = { actions.onSelectBuiltinProfile(null) },
                    )
                }
                if (state.builtinTemplates.isNotEmpty()) {
                    item(key = "templates-title") {
                        SectionLabel(stringResource(R.string.templates_section))
                    }
                    items(state.builtinTemplates) { release ->
                        BuiltinProfileRow(
                            title = release,
                            selected = state.activeUserProfile == null &&
                                state.activeBuiltinProfile == release,
                            onClick = { actions.onSelectBuiltinProfile(release) },
                        )
                    }
                }
                if (state.builtinProfiles.isNotEmpty()) {
                    item(key = "kernels-title") {
                        SectionLabel(stringResource(R.string.kernels_section))
                    }
                    items(state.builtinProfiles) { release ->
                        BuiltinProfileRow(
                            title = release,
                            selected = state.activeUserProfile == null &&
                                state.activeBuiltinProfile == release,
                            onClick = { actions.onSelectBuiltinProfile(release) },
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun SectionLabel(text: String) {
    Text(
        text = text,
        modifier = Modifier.padding(start = 4.dp, top = 8.dp),
        fontSize = 14.sp,
        fontWeight = FontWeight.SemiBold,
        color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
    )
}

@Composable
private fun BuiltinProfileRow(
    title: String,
    selected: Boolean,
    onClick: () -> Unit,
) {
    Card(modifier = Modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clickable(onClick = onClick)
                .padding(horizontal = 16.dp, vertical = 14.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                text = title,
                modifier = Modifier.weight(1f),
                fontSize = 14.sp,
                fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Normal,
                color = if (selected) OverrideHighlight
                else MiuixTheme.colorScheme.onSurface,
            )
            if (selected) {
                Icon(
                    imageVector = MiuixIcons.Basic.Check,
                    contentDescription = null,
                    tint = OverrideHighlight,
                    modifier = Modifier.size(18.dp),
                )
            }
        }
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
    var picker by remember { mutableStateOf<String?>(null) }
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
                .padding(paddingValues),
            contentPadding = PaddingValues(start = 12.dp, end = 12.dp, top = 8.dp, bottom = 12.dp),
        ) {
            item(key = "warning") {
                OverrideWarning()
            }
            item(key = "route") {
                Card(modifier = Modifier.padding(top = 8.dp)) {
                    Column {
                        ArrowPreference(
                            title = stringResource(R.string.route_label),
                            summary = state.profileRoute ?: stringResource(R.string.route_auto),
                            onClick = { picker = "route" },
                        )
                        ArrowPreference(
                            title = stringResource(R.string.fallback_label),
                            summary = state.profileFallback
                                ?.takeIf { it != "none" }
                                ?: stringResource(R.string.fallback_none),
                            onClick = { picker = "fallback" },
                        )
                    }
                }
            }
            item(key = "release") {
                Card(modifier = Modifier.padding(top = 8.dp)) {
                    Column(modifier = Modifier.padding(horizontal = 16.dp, vertical = 12.dp)) {
                        Text(
                            text = stringResource(
                                R.string.loaded_profile,
                                state.activeBuiltinProfile
                                    ?: state.profileOverrideRelease,
                            ),
                            style = MiuixTheme.textStyles.body2,
                            color = if (state.activeBuiltinProfile != null) {
                                FieldErrorHighlight
                            } else {
                                MiuixTheme.colorScheme.onSurfaceVariantSummary
                            },
                        )
                        if (state.activeBuiltinProfile != null) {
                            Text(
                                text = stringResource(
                                    R.string.target_kernel,
                                    state.profileOverrideRelease,
                                ),
                                modifier = Modifier.padding(top = 4.dp),
                                style = MiuixTheme.textStyles.body2,
                                color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                            )
                        }
                        state.activeUserProfile?.let { userProfile ->
                            Text(
                                text = stringResource(R.string.loaded_user_profile, userProfile),
                                modifier = Modifier.padding(top = 4.dp),
                                style = MiuixTheme.textStyles.body2,
                                color = OverrideHighlight,
                            )
                        }
                    }
                }
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
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(top = 4.dp),
                )
            }
        }
    }
    val pickerTitle = when (picker) {
        "route" -> stringResource(R.string.route_label)
        "fallback" -> stringResource(R.string.fallback_label)
        else -> null
    }
    if (pickerTitle != null) {
        OverlayDialog(
            show = true,
            title = pickerTitle,
            onDismissRequest = { picker = null },
            content = {
                val labels = if (picker == "route") {
                    listOf(stringResource(R.string.route_auto)) + ProfileConfig.Routes
                } else {
                    listOf(stringResource(R.string.fallback_none)) + ProfileConfig.Routes
                }
                labels.forEachIndexed { index, label ->
                    TextButton(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(bottom = 12.dp),
                        text = label,
                        onClick = {
                            if (picker == "route") {
                                actions.onRouteChanged(index)
                            } else {
                                actions.onFallbackChanged(index)
                            }
                            picker = null
                        },
                    )
                }
                TextButton(
                    modifier = Modifier.fillMaxWidth(),
                    text = stringResource(R.string.cancel),
                    onClick = { picker = null },
                )
            },
        )
    }
}

/** Warning card styled after the home-screen activation card, red exclamation. */
@Composable
private fun OverrideWarning() {
    val dark = isSystemInDarkTheme()
    Card(
        colors = CardDefaults.defaultColors(
            color = if (dark) Color(0xFF3B1F1F) else Color(0xFFFFE9E9),
        ),
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .height(110.dp),
        ) {
            Text(
                text = stringResource(R.string.debug_profile_override_warning),
                modifier = Modifier
                    .align(Alignment.TopStart)
                    .padding(start = 16.dp, top = 14.dp, end = 76.dp),
                fontSize = 18.sp,
                fontWeight = FontWeight.SemiBold,
                color = MiuixTheme.colorScheme.onSurface,
            )
            Icon(
                imageVector = Icons.Rounded.ErrorOutline,
                contentDescription = null,
                tint = if (dark) Color(0xFFFF6B6B) else Color(0xFFE53935),
                modifier = Modifier
                    .align(Alignment.BottomEnd)
                    .offset(x = 27.dp, y = 31.dp)
                    .size(110.dp),
            )
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
            val isExpanded = expanded[node.path] ?: true
            val arrowRotation by animateFloatAsState(
                targetValue = if (isExpanded) 90f else 0f,
                label = "group-arrow",
            )
            Column {
                Card(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(start = (depth * 12).dp, top = 8.dp),
                ) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable { onToggle(node.path, !isExpanded) }
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
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(start = (depth * 12).dp, top = 8.dp),
                singleLine = true,
            )
        }
    }
}
