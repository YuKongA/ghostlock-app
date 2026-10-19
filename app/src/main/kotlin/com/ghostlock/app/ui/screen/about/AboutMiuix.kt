package com.ghostlock.app.ui.screen.about

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.add
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.systemBars
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Icon as MaterialIcon
import androidx.compose.material3.IconButton as MaterialIconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text as MaterialText
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
import top.yukonga.miuix.kmp.basic.Card
import top.yukonga.miuix.kmp.basic.MiuixScrollBehavior
import top.yukonga.miuix.kmp.basic.Scaffold
import top.yukonga.miuix.kmp.basic.Text
import top.yukonga.miuix.kmp.basic.TopAppBar
import top.yukonga.miuix.kmp.preference.ArrowPreference
import top.yukonga.miuix.kmp.theme.MiuixTheme

/** 关于页（与其他页同一套页面骨架：标准 TopAppBar + 16dp 内容卡片）。 */
@Composable
fun AboutScreenMiuix(onBack: () -> Unit) {
    val scrollBehavior = MiuixScrollBehavior()
    val uriHandler = LocalUriHandler.current
    Scaffold(
        topBar = {
            TopAppBar(
                title = stringResource(R.string.about),
                scrollBehavior = scrollBehavior,
                navigationIcon = {
                    MaterialIconButton(onClick = onBack) {
                        MaterialIcon(
                            imageVector = Icons.AutoMirrored.Filled.ArrowBack,
                            contentDescription = stringResource(R.string.action_back),
                            tint = MiuixTheme.colorScheme.onBackground,
                        )
                    }
                },
            )
        },
        contentWindowInsets = WindowInsets.systemBars.add(WindowInsets.displayCutout).only(WindowInsetsSides.Horizontal),
    ) { innerPadding ->
        Column(
            modifier = Modifier
                .padding(innerPadding)
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Card(modifier = Modifier.fillMaxWidth()) {
                Row(
                    modifier = Modifier.fillMaxWidth().padding(16.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Surface(
                        shape = MaterialTheme.shapes.large,
                        color = colorResource(R.color.launcher_icon_background),
                    ) {
                        Image(
                            painter = painterResource(R.drawable.ic_launcher_foreground),
                            contentDescription = null,
                            modifier = Modifier.size(56.dp).padding(6.dp),
                        )
                    }
                    Column(modifier = Modifier.padding(start = 12.dp)) {
                        Text(text = stringResource(R.string.app_name), style = MiuixTheme.textStyles.title2)
                        Text(
                            text = "v${BuildConfig.VERSION_NAME} (${BuildConfig.VERSION_CODE})",
                            style = MiuixTheme.textStyles.footnote1,
                            color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
                        )
                    }
                }
            }

            SectionTitle(stringResource(R.string.about_project))
            Card(modifier = Modifier.fillMaxWidth()) {
                AboutLink(
                    title = stringResource(R.string.about_source_code),
                    summary = "github.com/YuKongA/ghostlock-app",
                    onClick = { uriHandler.openUri("https://github.com/YuKongA/ghostlock-app") },
                )
            }

            SectionTitle(stringResource(R.string.about_origins))
            Card(modifier = Modifier.fillMaxWidth()) {
                AboutLink("CyberMeowfia", "github.com/NebuSec/CyberMeowfia") {
                    uriHandler.openUri("https://github.com/NebuSec/CyberMeowfia")
                }
                AboutLink("ghostlock-oneplus", "github.com/JoinChang/ghostlock-oneplus") {
                    uriHandler.openUri("https://github.com/JoinChang/ghostlock-oneplus")
                }
                AboutLink("CVE-2026-43499-popsicle", "github.com/x-spy/CVE-2026-43499-popsicle") {
                    uriHandler.openUri("https://github.com/x-spy/CVE-2026-43499-popsicle")
                }
            }

            SectionTitle(stringResource(R.string.about_open_source))
            Card(modifier = Modifier.fillMaxWidth()) {
                AboutLink("Apache License 2.0", "github.com/YuKongA/ghostlock-app/LICENSE") {
                    uriHandler.openUri("https://github.com/YuKongA/ghostlock-app/blob/main/LICENSE")
                }
                AboutLink("miuix", "github.com/compose-miuix-ui/miuix") {
                    uriHandler.openUri("https://github.com/compose-miuix-ui/miuix")
                }
                AboutLink("Shizuku", "github.com/RikkaApps/Shizuku") {
                    uriHandler.openUri("https://github.com/RikkaApps/Shizuku")
                }
                AboutLink("Typesafe Config", "github.com/lightbend/config") {
                    uriHandler.openUri("https://github.com/lightbend/config")
                }
                AboutLink("Apache Commons Compress", "commons.apache.org/proper/commons-compress") {
                    uriHandler.openUri("https://commons.apache.org/proper/commons-compress/")
                }
            }

            Text(
                text = stringResource(R.string.opensource_info),
                modifier = Modifier.fillMaxWidth().padding(vertical = 8.dp),
                style = MiuixTheme.textStyles.footnote1,
                color = MiuixTheme.colorScheme.onSurfaceVariantSummary,
            )
            Spacer(Modifier.height(12.dp))
        }
    }
}

@Composable
private fun SectionTitle(text: String) {
    Text(
        text = text,
        modifier = Modifier.padding(start = 4.dp, top = 4.dp),
        style = MiuixTheme.textStyles.footnote1,
        color = MiuixTheme.colorScheme.primary,
    )
}

@Composable
private fun AboutLink(title: String, summary: String, onClick: () -> Unit) {
    ArrowPreference(title = title, summary = summary, onClick = onClick)
}
