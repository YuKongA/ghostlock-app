package com.ghostlock.app.ui.screen.home

import androidx.compose.runtime.Immutable
import com.ghostlock.app.domain.privilege.AttackExecutorKind
import com.ghostlock.app.domain.update.AppUpdateInfo

/** Semantic summary of the currently selected GhostLock backend. */
@Immutable
data class SelectedBackendUiState(
    val cveLabel: String,
    val statusText: String,
    val isReady: Boolean,
)

/** Renderer-neutral executor facts shared by the home summary and privilege dialog. */
@Immutable
data class PrivilegeUiState(
    val selectedExecutor: AttackExecutorKind,
    val displayedExecutor: AttackExecutorKind,
    val appUid: Int,
    val shizukuAvailable: Boolean,
    val shizukuGranted: Boolean,
) {
    /** @return Stable executor label consumed by existing localized format resources. */
    val displayedLabel: String
        get() = if (displayedExecutor == AttackExecutorKind.SHIZUKU) "shizuku" else "app"

    /** @return Persisted executor label used by the semantic dialog selection marker. */
    val selectedLabel: String
        get() = if (selectedExecutor == AttackExecutorKind.SHIZUKU) "shizuku" else "app"

    /** @return Linux uid displayed for the currently usable compact-summary executor. */
    val displayedUid: Int
        get() = if (displayedExecutor == AttackExecutorKind.SHIZUKU) 2000 else appUid
}

/** Renderer-neutral home-page state shared by Material and Miuix implementations. */
@Immutable
data class HomeUiState(
    val selectedBackend: SelectedBackendUiState,
    val isPrBuild: Boolean,
    val checkUpdateEnabled: Boolean,
    val latestVersionInfo: AppUpdateInfo,
    val currentAppVersionCode: Long,
    val systemInfo: SystemInfo,
    val privilege: PrivilegeUiState,
) {
    /** @return True when the update source reports a newer application build. */
    val hasUpdate: Boolean
        get() = latestVersionInfo.versionCode > currentAppVersionCode
}

/** Renderer callbacks that remain external to the immutable home state. */
@Immutable
data class HomeActions(
    val onOpenUrl: (String) -> Unit,
    val onOpenPrivilege: () -> Unit,
)
