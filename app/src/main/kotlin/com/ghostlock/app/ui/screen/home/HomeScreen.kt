package com.ghostlock.app.ui.screen.home

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.unit.Dp
import androidx.lifecycle.compose.LifecycleResumeEffect
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.ghostlock.app.GhostLockApp
import com.ghostlock.app.R
import com.ghostlock.app.domain.privilege.AttackExecutorKind
import com.ghostlock.app.domain.privilege.ExecutorSelectionResult
import com.ghostlock.app.ui.LocalMainPagerState
import com.ghostlock.app.ui.LocalUiMode
import com.ghostlock.app.ui.UiMode
import com.ghostlock.app.ui.backend.BackendStatusTone
import com.ghostlock.app.ui.component.statuscard.BackendStatusCard
import com.ghostlock.app.ui.component.statuscard.BackendStatusCardModel
import com.ghostlock.app.ui.component.dialog.PrivilegeAvailability
import com.ghostlock.app.ui.component.dialog.PrivilegeDialog
import com.ghostlock.app.ui.component.dialog.PrivilegeDialogPage
import com.ghostlock.app.ui.component.dialog.PrivilegeDialogState
import com.ghostlock.app.ui.component.dialog.PrivilegeExecutorRow
import com.ghostlock.app.ui.navigation3.Navigator
import com.ghostlock.app.ui.navigation3.Route
import com.ghostlock.app.ui.viewmodel.HomeViewModel

@Composable
fun HomePager(
    navigator: Navigator,
    bottomInnerPadding: Dp,
    isCurrentPage: Boolean = true,
) {
    val context = LocalContext.current
    val app = context.applicationContext as GhostLockApp
    val viewModel = viewModel<HomeViewModel>(factory = app.container.viewModelFactory)
    val uiState by viewModel.uiState.collectAsStateWithLifecycle()
    val uriHandler = LocalUriHandler.current
    val latestIsCurrentPage by rememberUpdatedState(isCurrentPage)
    val initialResumeHandled = rememberSaveable { mutableStateOf(false) }
    var showPrivilege by rememberSaveable { mutableStateOf(false) }
    var privilegePage by rememberSaveable { mutableStateOf(PrivilegeDialogPage.OVERVIEW) }

    LaunchedEffect(isCurrentPage) {
        if (isCurrentPage) {
            viewModel.refresh()
        }
    }

    LifecycleResumeEffect(Unit) {
        if (initialResumeHandled.value && latestIsCurrentPage) {
            viewModel.refresh()
        }
        initialResumeHandled.value = true
        onPauseOrDispose { }
    }

    val actions = HomeActions(
        onOpenUrl = uriHandler::openUri,
        onOpenPrivilege = {
            privilegePage = PrivilegeDialogPage.OVERVIEW
            showPrivilege = true
        },
    )

    when (LocalUiMode.current) {
        UiMode.Miuix -> HomePagerMiuix(
            state = uiState,
            actions = actions,
            bottomInnerPadding = bottomInnerPadding,
        )

        UiMode.Material -> HomePagerMaterial(
            state = uiState,
            actions = actions,
            bottomInnerPadding = bottomInnerPadding,
        )
    }

    if (showPrivilege) {
        val privilege = uiState.privilege
        PrivilegeDialog(
            state = PrivilegeDialogState(
                page = privilegePage,
                rows = listOf(
                    PrivilegeExecutorRow(
                        label = "app",
                        uid = privilege.appUid,
                        availability = PrivilegeAvailability.APP,
                        available = true,
                    ),
                    PrivilegeExecutorRow(
                        label = "shizuku",
                        uid = 2000,
                        availability = when {
                            !privilege.shizukuAvailable -> PrivilegeAvailability.UNAVAILABLE
                            privilege.shizukuGranted -> PrivilegeAvailability.GRANTED
                            else -> PrivilegeAvailability.DENIED
                        },
                        available = privilege.shizukuGranted,
                    ),
                ),
                selectedLabel = privilege.selectedLabel,
            ),
            onDismiss = { showPrivilege = false },
            onSelect = { label ->
                val kind = if (label == "shizuku") {
                    AttackExecutorKind.SHIZUKU
                } else {
                    AttackExecutorKind.APP
                }
                when (viewModel.selectExecutor(kind)) {
                    ExecutorSelectionResult.SELECTED,
                    ExecutorSelectionResult.PERMISSION_REQUESTED,
                    -> privilegePage = PrivilegeDialogPage.OVERVIEW

                    ExecutorSelectionResult.UNAVAILABLE ->
                        privilegePage = PrivilegeDialogPage.SHIZUKU_MISSING
                }
            },
            onBack = { privilegePage = PrivilegeDialogPage.OVERVIEW },
        )
    }
}

/**
 * Renders the selected backend through the single reusable semantic status-card component.
 *
 * @param state Renderer-neutral home state produced by HomeViewModel.
 * @note Navigation semantics are shared by both skins: ready opens Run, otherwise Paths.
 */
@Composable
internal fun HomeBackendStatusCard(state: HomeUiState) {
    val mainPager = LocalMainPagerState.current
    val backend = state.selectedBackend
    BackendStatusCard(
        model = BackendStatusCardModel(
            cveLabel = backend.cveLabel,
            statusText = backend.statusText,
            tone = if (backend.isReady) {
                BackendStatusTone.READY
            } else {
                BackendStatusTone.NOT_READY
            },
            navigationTextRes = if (backend.isReady) {
                R.string.home_nav_run
            } else {
                R.string.home_nav_paths
            },
            onClick = {
                mainPager.animateToPage(if (backend.isReady) 2 else 1)
            },
        ),
    )
}
