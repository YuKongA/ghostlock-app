package com.ghostlock.app.ui.screen.about

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
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
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
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.colorResource
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.ghostlock.app.BuildConfig
import com.ghostlock.app.R
import com.ghostlock.app.ui.component.material.ExpressiveScaffold
import com.ghostlock.app.ui.component.material.SegmentedColumn
import com.ghostlock.app.ui.component.material.SegmentedListItem
import com.ghostlock.app.ui.component.material.TonalCard
import com.ghostlock.app.ui.component.material.expressiveTopAppBarColors

/** 关于页（与其他页同一套页面骨架：标准 TopAppBar + 16dp 内容卡片）。 */
@Composable
fun AboutScreenMaterial(onBack: () -> Unit) {
    val uriHandler = LocalUriHandler.current
    val scrollBehavior = TopAppBarDefaults.exitUntilCollapsedScrollBehavior(rememberTopAppBarState())
    ExpressiveScaffold(
        topBar = {
            LargeFlexibleTopAppBar(
                title = { Text(stringResource(R.string.about)) },
                colors = expressiveTopAppBarColors(),
                windowInsets = WindowInsets.safeDrawing.only(WindowInsetsSides.Top + WindowInsetsSides.Horizontal),
                scrollBehavior = scrollBehavior,
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(Icons.AutoMirrored.Filled.ArrowBack, contentDescription = stringResource(R.string.action_back))
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
                                "v${BuildConfig.VERSION_NAME} (${BuildConfig.VERSION_CODE})",
                                style = MaterialTheme.typography.bodyMedium,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                    }
                }
            }
            item { SectionTitle(stringResource(R.string.about_project)) }
            item {
                LinkGroup {
                    LinkRow(stringResource(R.string.about_source_code), "github.com/YuKongA/ghostlock-app") {
                        uriHandler.openUri("https://github.com/YuKongA/ghostlock-app")
                    }
                }
            }
            item { SectionTitle(stringResource(R.string.about_origins)) }
            item {
                LinkGroup {
                    LinkRow("CyberMeowfia", "github.com/NebuSec/CyberMeowfia") {
                        uriHandler.openUri("https://github.com/NebuSec/CyberMeowfia")
                    }
                    LinkRow("ghostlock-oneplus", "github.com/JoinChang/ghostlock-oneplus") {
                        uriHandler.openUri("https://github.com/JoinChang/ghostlock-oneplus")
                    }
                    LinkRow("CVE-2026-43499-popsicle", "github.com/x-spy/CVE-2026-43499-popsicle") {
                        uriHandler.openUri("https://github.com/x-spy/CVE-2026-43499-popsicle")
                    }
                }
            }
            item { SectionTitle(stringResource(R.string.about_open_source)) }
            item {
                LinkGroup {
                    LinkRow("Apache License 2.0", "github.com/YuKongA/ghostlock-app/LICENSE") {
                        uriHandler.openUri("https://github.com/YuKongA/ghostlock-app/blob/main/LICENSE")
                    }
                    LinkRow("miuix", "github.com/compose-miuix-ui/miuix") {
                        uriHandler.openUri("https://github.com/compose-miuix-ui/miuix")
                    }
                    LinkRow("Shizuku", "github.com/RikkaApps/Shizuku") {
                        uriHandler.openUri("https://github.com/RikkaApps/Shizuku")
                    }
                    LinkRow("Typesafe Config", "github.com/lightbend/config") {
                        uriHandler.openUri("https://github.com/lightbend/config")
                    }
                    LinkRow("Apache Commons Compress", "commons.apache.org/proper/commons-compress") {
                        uriHandler.openUri("https://commons.apache.org/proper/commons-compress/")
                    }
                }
            }
            item {
                Text(
                    text = stringResource(R.string.opensource_info),
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
