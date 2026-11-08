package com.ghostlock.app.ui.screen.about

import com.ghostlock.app.ui.icon.Icons as ProjectIcons

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.safeDrawing
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LargeFlexibleTopAppBar
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.material3.rememberTopAppBarState
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.colorResource
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.ghostlock.app.R
import com.ghostlock.app.ui.component.material.ExpressiveScaffold
import com.ghostlock.app.ui.component.material.SegmentedColumn
import com.ghostlock.app.ui.component.material.SegmentedListItem
import com.ghostlock.app.ui.component.material.TonalCard
import com.ghostlock.app.ui.component.material.expressiveTopAppBarColors

/** 关于页（与其他页同一套页面骨架：标准 TopAppBar + 16dp 内容卡片）。 */
@Composable
fun AboutScreenMaterial(state: AboutUiState, actions: AboutActions) {
    val scrollBehavior = TopAppBarDefaults.exitUntilCollapsedScrollBehavior(rememberTopAppBarState())
    ExpressiveScaffold(
        topBar = {
            LargeFlexibleTopAppBar(
                title = { Text(stringResource(R.string.about)) },
                colors = expressiveTopAppBarColors(),
                windowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
                scrollBehavior = scrollBehavior,
                navigationIcon = {
                    IconButton(onClick = actions.onBack) {
                        Icon(ProjectIcons.AutoMirrored.Filled.ArrowBack.material, contentDescription = stringResource(R.string.action_back))
                    }
                },
            )
        },
        contentWindowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
    ) { innerPadding ->
        LazyColumn(
            modifier = Modifier.fillMaxSize().padding(innerPadding),
            contentPadding = PaddingValues(start = 16.dp, end = 16.dp, top = 12.dp, bottom = 24.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            item {
                TonalCard {
                    Row(modifier = Modifier.fillMaxWidth().padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
                        Surface(shape = MaterialTheme.shapes.large, color = colorResource(R.color.launcher_icon_background)) {
                            Image(
                                painter = painterResource(R.drawable.ic_launcher_foreground),
                                contentDescription = null,
                                modifier = Modifier.size(56.dp).padding(6.dp),
                            )
                        }
                        Column(modifier = Modifier.padding(start = 12.dp)) {
                            Text(stringResource(R.string.app_name), style = MaterialTheme.typography.titleLarge)
                            Text(
                                state.versionLabel,
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                    }
                }
            }
            state.sections.forEach { section ->
                item { SectionTitle(section.title) }
                item {
                    LinkGroup {
                        section.links.forEach { link ->
                            LinkRow(link.title, link.summary) {
                                actions.onOpenUrl(link.url)
                            }
                        }
                    }
                }
            }
            item {
                Text(
                    text = state.openSourceInfo,
                    modifier = Modifier.fillMaxWidth().padding(vertical = 8.dp),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            item { Spacer(Modifier.height(12.dp)) }
        }
    }
}

@Composable
private fun SectionTitle(text: String) {
    Text(
        text = text,
        modifier = Modifier.padding(start = 4.dp, top = 4.dp),
        style = MaterialTheme.typography.labelLarge,
        color = MaterialTheme.colorScheme.primary,
    )
}

@Composable
private fun LinkGroup(content: @Composable () -> Unit) {
    TonalCard {
        SegmentedColumn(Modifier.fillMaxWidth()) {
            item {
                Column {
                    content()
                }
            }
        }
    }
}

@Composable
private fun LinkRow(title: String, summary: String, onClick: () -> Unit) {
    SegmentedListItem(
        onClick = onClick,
        headlineContent = { Text(title) },
        supportingContent = { Text(summary) },
    )
}
