package com.ghostlock.app.ui.screen.module

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.add
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.systemBars
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Memory
import androidx.compose.material3.LargeFlexibleTopAppBar
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.rememberTopAppBarState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.ghostlock.app.GhostLockApp
import com.ghostlock.app.R
import com.ghostlock.app.ui.LocalUiMode
import com.ghostlock.app.ui.UiMode
import com.ghostlock.app.ui.component.material.ExpressiveScaffold
import com.ghostlock.app.ui.component.material.SegmentedColumn
import com.ghostlock.app.ui.component.material.SegmentedDropdownItem
import com.ghostlock.app.ui.component.material.TonalCard
import com.ghostlock.app.ui.component.material.expressiveTopAppBarColors
import com.ghostlock.app.ui.backend.BackendConfigRenderer
import com.ghostlock.app.ui.viewmodel.PathViewModel
import com.ghostlock.app.ui.viewmodel.FinishEditorViewModel
import com.ghostlock.app.ui.viewmodel.FinishSettingsViewModel
import com.ghostlock.app.ui.screen.module.backends.FinishCardMaterial
import com.ghostlock.app.ui.screen.module.backends.FinishCardMiuix
import com.ghostlock.app.ui.screen.module.backends.FinishEditorUiState
import com.ghostlock.app.ui.screen.module.backends.FinishSettingsCardMaterial
import com.ghostlock.app.ui.screen.module.backends.FinishSettingsCardMiuix
import com.ghostlock.app.ui.screen.module.backends.FinishSettingsUiState
import top.yukonga.miuix.kmp.basic.Card
import top.yukonga.miuix.kmp.basic.MiuixScrollBehavior
import top.yukonga.miuix.kmp.basic.Scaffold as MiuixScaffold
import top.yukonga.miuix.kmp.basic.Text as MiuixText
import top.yukonga.miuix.kmp.basic.TopAppBar as MiuixTopAppBar
import top.yukonga.miuix.kmp.basic.DropdownEntry
import top.yukonga.miuix.kmp.basic.DropdownItem
import top.yukonga.miuix.kmp.preference.OverlayDropdownPreference
import top.yukonga.miuix.kmp.theme.MiuixTheme

/** Selects the page-level renderer while preserving the existing LocalUiMode appearance branch. */
@Composable
fun BackendPager(
    bottomInnerPadding: Dp,
    isCurrentPage: Boolean = true,
    onOpenFinish: () -> Unit,
    onOpenFinishSettings: () -> Unit,
) {
    val context = LocalContext.current
    val app = context.applicationContext as GhostLockApp
    val pathViewModel = viewModel<PathViewModel>(factory = app.container.viewModelFactory)
    val finishEditorViewModel = viewModel<FinishEditorViewModel>(factory = app.container.viewModelFactory)
    val finishSettingsViewModel = viewModel<FinishSettingsViewModel>(factory = app.container.viewModelFactory)
    val state by pathViewModel.uiState.collectAsStateWithLifecycle()
    val finishEditorState by finishEditorViewModel.uiState.collectAsStateWithLifecycle()
    val finishSettingsState by finishSettingsViewModel.uiState.collectAsStateWithLifecycle()
    LaunchedEffect(isCurrentPage) {
        if (isCurrentPage) {
            pathViewModel.refresh()
            finishEditorViewModel.refresh()
            finishSettingsViewModel.refresh()
        }
    }
    val actions = BackendPageActions(
        onSelectBackend = pathViewModel::selectBackend,
        onOpenFinish = {
            finishEditorViewModel.openEditor()
            onOpenFinish()
        },
        onOpenFinishSettings = onOpenFinishSettings,
    )
    when (LocalUiMode.current) {
        UiMode.Material -> BackendPageMaterial(
            state = state,
            actions = actions,
            finishEditorState = finishEditorState,
            finishSettingsState = finishSettingsState,
            configRenderer = app.container.backendConfigRenderer,
            bottomInnerPadding = bottomInnerPadding,
        )
        UiMode.Miuix -> BackendPageMiuix(
            state = state,
            actions = actions,
            finishEditorState = finishEditorState,
            finishSettingsState = finishSettingsState,
            configRenderer = app.container.backendConfigRenderer,
            bottomInnerPadding = bottomInnerPadding,
        )
    }
}

/**
 *  「路径」页：顶部展示全部 backend；仅 selectable spec 可选择，disabled spec 灰显。
 *
 *  配置项来自 backends/ 下每个 backend 自己的 .kt 文件；本页只做渲染。
 *
 *  2026-10-26（task-21）：路径页只负责配置；运行入口移到运行页，由 `BackendRunGate`
 *  统一决定 allow/deny（本页不再有 CVE-43499 运行卡）。
 */
@Composable
fun BackendPageMaterial(
    state: BackendPageUiState,
    actions: BackendPageActions,
    finishEditorState: FinishEditorUiState,
    finishSettingsState: FinishSettingsUiState,
    configRenderer: BackendConfigRenderer,
    bottomInnerPadding: Dp,
) {
    val selected = state.selectedIndex
    val titles = state.options.map { it.title }
    val scrollBehavior = TopAppBarDefaults.exitUntilCollapsedScrollBehavior(rememberTopAppBarState())

    ExpressiveScaffold(
        topBar = {
            LargeFlexibleTopAppBar(
                title = { Text(stringResource(R.string.paths)) },
                colors = expressiveTopAppBarColors(),
                windowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
                scrollBehavior = scrollBehavior,
            )
        },
        contentWindowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .padding(innerPadding)
                .nestedScroll(scrollBehavior.nestedScrollConnection)
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp)
                .padding(top = 16.dp)
                .padding(bottom = bottomInnerPadding),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            SegmentedColumn(modifier = Modifier.fillMaxWidth()) {
                item {
                    SegmentedDropdownItem(
                        icon = Icons.Filled.Memory,
                        title = stringResource(R.string.backend_label),
                        summary = titles.getOrElse(selected) { "" },
                        items = titles,
                        selectedIndex = selected,
                        itemEnabled = { state.options.getOrNull(it)?.selectable == true },
                        onItemSelected = { index ->
                            state.options.getOrNull(index)?.let { actions.onSelectBackend(it.key) }
                        },
                    )
                }
            }
            FinishCardMaterial(finishEditorState, actions.onOpenFinish)
            FinishSettingsCardMaterial(finishSettingsState, actions.onOpenFinishSettings)
            TonalCard {
                /* 唯一渲染路径：backend 自己的 ConfigCard（默认卡内部读 rows）。 */
                configRenderer.render(state.selectedKey)
            }
        }
    }
}

@Composable
fun BackendPageMiuix(
    state: BackendPageUiState,
    actions: BackendPageActions,
    finishEditorState: FinishEditorUiState,
    finishSettingsState: FinishSettingsUiState,
    configRenderer: BackendConfigRenderer,
    bottomInnerPadding: Dp,
) {
    val selected = state.selectedIndex
    val backendEntry = DropdownEntry(
        items = state.options.mapIndexed { index, option ->
            DropdownItem(
                text = option.title,
                enabled = option.selectable,
                selected = index == selected,
                onClick = { if (option.selectable) actions.onSelectBackend(option.key) },
            )
        },
    )
    val selectedTitle = state.options.getOrNull(selected)?.title.orEmpty()
    val scrollBehavior = MiuixScrollBehavior()

    MiuixScaffold(
        topBar = {
            MiuixTopAppBar(
                title = stringResource(R.string.paths),
                scrollBehavior = scrollBehavior,
            )
        },
        contentWindowInsets = WindowInsets.systemBars.add(WindowInsets.displayCutout).only(WindowInsetsSides.Horizontal),
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(innerPadding)
                .nestedScroll(scrollBehavior.nestedScrollConnection)
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp)
                .padding(top = 16.dp)
                .padding(bottom = bottomInnerPadding),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Card(modifier = Modifier.fillMaxWidth()) {
                OverlayDropdownPreference(
                    startAction = {
                        androidx.compose.material3.Icon(
                            imageVector = Icons.Filled.Memory,
                            contentDescription = null,
                            tint = MiuixTheme.colorScheme.onBackground,
                        )
                    },
                    title = stringResource(R.string.backend_label),
                    summary = selectedTitle,
                    entry = backendEntry,
                )
            }
            FinishCardMiuix(finishEditorState, actions.onOpenFinish)
            FinishSettingsCardMiuix(finishSettingsState, actions.onOpenFinishSettings)
            Card(modifier = Modifier.fillMaxWidth()) {
                /* 唯一渲染路径：backend 自己的 ConfigCard（默认卡内部读 rows）。 */
                configRenderer.render(state.selectedKey)
            }

        }
    }
}
