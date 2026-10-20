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
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.ghostlock.app.R
import com.ghostlock.app.ui.component.material.ExpressiveScaffold
import com.ghostlock.app.ui.component.material.SegmentedColumn
import com.ghostlock.app.ui.component.material.SegmentedDropdownItem
import com.ghostlock.app.ui.component.material.SegmentedListItem
import com.ghostlock.app.ui.component.material.TonalCard
import com.ghostlock.app.ui.component.material.expressiveTopAppBarColors
import com.ghostlock.app.ui.screen.module.backends.BackendSelection
import com.ghostlock.app.ui.screen.module.backends.Backends
import com.ghostlock.app.ui.screen.module.backends.FinishCard
import com.ghostlock.app.ui.screen.module.backends.FinishSettingsCard
import top.yukonga.miuix.kmp.basic.Card
import top.yukonga.miuix.kmp.basic.MiuixScrollBehavior
import top.yukonga.miuix.kmp.basic.Scaffold as MiuixScaffold
import top.yukonga.miuix.kmp.basic.Text as MiuixText
import top.yukonga.miuix.kmp.basic.TopAppBar as MiuixTopAppBar
import top.yukonga.miuix.kmp.preference.OverlayDropdownPreference
import top.yukonga.miuix.kmp.theme.MiuixTheme

/**
 *  「路径」页（2026-10-18）：顶部选后端（CVE 编号），下面显示对应后端的配置项。
 *
 *  配置项来自 backends/ 下每个 backend 自己的 .kt 文件；本页只做渲染。
 */
@Composable
fun BackendPageMaterial(bottomInnerPadding: Dp) {
    var selected by remember { mutableIntStateOf(BackendSelection.index) }
        val specs = Backends.all
    val titles = specs.map { stringResource(it.titleRes) }
    val scrollBehavior = TopAppBarDefaults.exitUntilCollapsedScrollBehavior(rememberTopAppBarState())

    ExpressiveScaffold(
        topBar = {
            LargeFlexibleTopAppBar(
                title = { Text(stringResource(R.string.module)) },
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
                        summary = titles[selected],
                        items = titles,
                        selectedIndex = selected,
                        onItemSelected = { selected = it; BackendSelection.index = it },
                    )
                }
            }
            FinishCard(miuix = false, onOpen = { com.ghostlock.app.ui.PageChrome.showFinish = true })
            FinishSettingsCard(miuix = false, onOpen = { com.ghostlock.app.ui.PageChrome.showFinishSettings = true })
            TonalCard {
                val custom = specs[selected].customConfig
                if (custom != null) {
                    custom(false)
                } else {
                SegmentedColumn(modifier = Modifier.fillMaxWidth()) {
                    for (row in specs[selected].rows()) {
                        item {
                            SegmentedListItem(
                                headlineContent = { Text(stringResource(row.label)) },
                                supportingContent = { Text(row.value) },
                            )
                        }
                    }
                }
                }
            }
        }
    }
}

@Composable
fun BackendPageMiuix(bottomInnerPadding: Dp) {
    var selected by remember { mutableIntStateOf(BackendSelection.index) }
        val specs = Backends.all
    val titles = specs.map { stringResource(it.titleRes) }
    val scrollBehavior = MiuixScrollBehavior()

    MiuixScaffold(
        topBar = {
            MiuixTopAppBar(
                title = stringResource(R.string.module),
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
                    summary = stringResource(R.string.backend_label),
                    items = titles,
                    selectedIndex = selected,
                    onSelectedIndexChange = { selected = it; BackendSelection.index = it },
                )
            }
            FinishCard(miuix = true, onOpen = { com.ghostlock.app.ui.PageChrome.showFinish = true })
            FinishSettingsCard(miuix = true, onOpen = { com.ghostlock.app.ui.PageChrome.showFinishSettings = true })
            Card(modifier = Modifier.fillMaxWidth()) {
                val custom = specs[selected].customConfig
                if (custom != null) {
                    custom(true)
                } else {
                Column {
                    for (row in specs[selected].rows()) {
                        top.yukonga.miuix.kmp.basic.BasicComponent(
                            title = stringResource(row.label),
                            summary = row.value,
                        )
                    }
                }
                }
            }
        }
    }
}
