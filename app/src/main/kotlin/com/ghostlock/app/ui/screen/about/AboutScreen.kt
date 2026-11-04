package com.ghostlock.app.ui.screen.about

import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.compose.dropUnlessResumed
import com.ghostlock.app.BuildConfig
import com.ghostlock.app.R
import com.ghostlock.app.ui.LocalUiMode
import com.ghostlock.app.ui.UiMode
import com.ghostlock.app.ui.navigation3.LocalNavigator

/** One renderer-neutral external link shown in the About page. */
data class AboutLinkUiState(
    val title: String,
    val summary: String,
    val url: String,
)

/** One ordered About-page section shared by both appearance renderers. */
data class AboutSectionUiState(
    val title: String,
    val links: List<AboutLinkUiState>,
)

/** Shared About-page content; renderers own layout only. */
data class AboutUiState(
    val versionLabel: String,
    val sections: List<AboutSectionUiState>,
    val openSourceInfo: String,
)

/** Shared About-page actions. */
data class AboutActions(
    val onBack: () -> Unit,
    val onOpenUrl: (String) -> Unit,
)

/** About-page binding that constructs content and actions once before the appearance branch. */
@Composable
fun AboutScreen() {
    val navigator = LocalNavigator.current
    val uriHandler = LocalUriHandler.current
    val state = defaultAboutUiState()
    val actions = AboutActions(
        onBack = dropUnlessResumed { navigator.pop() },
        onOpenUrl = uriHandler::openUri,
    )
    when (LocalUiMode.current) {
        UiMode.Miuix -> AboutScreenMiuix(state, actions)
        UiMode.Material -> AboutScreenMaterial(state, actions)
    }
}

/** @return Localized About-page content with one authoritative ordered URL list. */
@Composable
internal fun defaultAboutUiState(): AboutUiState = AboutUiState(
    versionLabel = "v${BuildConfig.VERSION_NAME} (${BuildConfig.VERSION_CODE})",
    sections = listOf(
        AboutSectionUiState(
            title = stringResource(R.string.about_project),
            links = listOf(
                AboutLinkUiState(
                    stringResource(R.string.about_source_code),
                    "github.com/YuKongA/ghostlock-app",
                    "https://github.com/YuKongA/ghostlock-app",
                ),
            ),
        ),
        AboutSectionUiState(
            title = stringResource(R.string.about_origins),
            links = listOf(
                AboutLinkUiState(
                    "CyberMeowfia",
                    "github.com/NebuSec/CyberMeowfia",
                    "https://github.com/NebuSec/CyberMeowfia",
                ),
                AboutLinkUiState(
                    "ghostlock-oneplus",
                    "github.com/JoinChang/ghostlock-oneplus",
                    "https://github.com/JoinChang/ghostlock-oneplus",
                ),
                AboutLinkUiState(
                    "CVE-2026-43499-popsicle",
                    "github.com/x-spy/CVE-2026-43499-popsicle",
                    "https://github.com/x-spy/CVE-2026-43499-popsicle",
                ),
            ),
        ),
        AboutSectionUiState(
            title = stringResource(R.string.about_open_source),
            links = listOf(
                AboutLinkUiState(
                    "Apache License 2.0",
                    "github.com/YuKongA/ghostlock-app/LICENSE",
                    "https://github.com/YuKongA/ghostlock-app/blob/main/LICENSE",
                ),
                AboutLinkUiState(
                    "miuix",
                    "github.com/compose-miuix-ui/miuix",
                    "https://github.com/compose-miuix-ui/miuix",
                ),
                AboutLinkUiState(
                    "Shizuku",
                    "github.com/RikkaApps/Shizuku",
                    "https://github.com/RikkaApps/Shizuku",
                ),
                AboutLinkUiState(
                    "Typesafe Config",
                    "github.com/lightbend/config",
                    "https://github.com/lightbend/config",
                ),
                AboutLinkUiState(
                    "Apache Commons Compress",
                    "commons.apache.org/proper/commons-compress",
                    "https://commons.apache.org/proper/commons-compress/",
                ),
            ),
        ),
    ),
    openSourceInfo = stringResource(R.string.opensource_info),
)
