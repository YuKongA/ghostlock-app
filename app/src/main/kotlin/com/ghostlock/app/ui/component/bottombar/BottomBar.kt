package com.ghostlock.app.ui.component.bottombar

import androidx.annotation.StringRes
import androidx.compose.foundation.pager.PagerState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Route
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.outlined.Home
import androidx.compose.material.icons.outlined.PlayArrow
import androidx.compose.material.icons.outlined.Route
import androidx.compose.material.icons.outlined.Settings
import androidx.compose.material.icons.rounded.Cottage
import androidx.compose.material.icons.rounded.PlayArrow as RoundedPlayArrow
import androidx.compose.material.icons.rounded.Route as RoundedRoute
import androidx.compose.material.icons.rounded.Settings as RoundedSettings
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import com.ghostlock.app.R
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.job
import kotlinx.coroutines.launch
import com.ghostlock.app.ui.LocalUiMode
import com.ghostlock.app.ui.UiMode
import com.ghostlock.app.ui.util.shouldShowSplitPane
import top.yukonga.miuix.kmp.blur.Backdrop
import top.yukonga.miuix.kmp.blur.LayerBackdrop
import top.yukonga.miuix.kmp.utils.springAnimateToPage

class MainPagerState(
    val pagerState: PagerState,
    private val coroutineScope: CoroutineScope,
    private val animatePageChanges: Boolean,
) {
    var selectedPage by mutableIntStateOf(pagerState.currentPage)
        private set

    var isNavigating by mutableStateOf(false)
        private set

    private var navJob: Job? = null

    fun animateToPage(targetIndex: Int) {
        if (targetIndex == selectedPage) return

        navJob?.cancel()

        selectedPage = targetIndex
        isNavigating = true

        navJob = coroutineScope.launch {
            val myJob = coroutineContext.job
            try {
                if (animatePageChanges) {
                    pagerState.springAnimateToPage(targetIndex)
                } else {
                    pagerState.scrollToPage(targetIndex)
                }
            } finally {
                if (navJob == myJob) {
                    isNavigating = false
                    if (pagerState.currentPage != targetIndex) {
                        selectedPage = pagerState.currentPage
                    }
                }
            }
        }
    }

    fun syncPage() {
        if (!isNavigating && selectedPage != pagerState.currentPage) {
            selectedPage = pagerState.currentPage
        }
    }
}

@Composable
fun rememberMainPagerState(
    pagerState: PagerState,
    coroutineScope: CoroutineScope = rememberCoroutineScope(),
    animatePageChanges: Boolean = true,
): MainPagerState {
    return remember(pagerState, coroutineScope, animatePageChanges) {
        MainPagerState(pagerState, coroutineScope, animatePageChanges)
    }
}

@Immutable
data class NavigationBadgeState(
    val byPage: Map<Int, NavigationBadge> = emptyMap(),
)

/** Neutral badge model keyed by the caller-owned page index. */
@Immutable
data class NavigationBadge(
    val count: Int,
    val accent: Boolean = false,
)

/** Shared destination model rendered by both Material and Miuix navigation chrome. */
enum class MainNavigationDestination(
    @get:StringRes val label: Int,
    val materialSelectedIcon: ImageVector,
    val materialUnselectedIcon: ImageVector,
    val miuixIcon: ImageVector,
) {
    Home(R.string.home, Icons.Filled.Home, Icons.Outlined.Home, Icons.Rounded.Cottage),
    Backend(R.string.paths, Icons.Filled.Route, Icons.Outlined.Route, Icons.Rounded.RoundedRoute),
    Run(R.string.run_page_title, Icons.Filled.PlayArrow, Icons.Outlined.PlayArrow, Icons.Rounded.RoundedPlayArrow),
    Settings(R.string.settings, Icons.Filled.Settings, Icons.Outlined.Settings, Icons.Rounded.RoundedSettings),
}

internal enum class BadgeTone { Alert, Accent }

@Immutable
internal data class NavBadge(val count: Int, val tone: BadgeTone)

/** Resolves one caller-owned page badge into the shared renderer model. */
internal fun badgeFor(index: Int, state: NavigationBadgeState): NavBadge? {
    val badge = state.byPage[index]?.takeIf { it.count > 0 } ?: return null
    return NavBadge(
        count = badge.count,
        tone = if (badge.accent) BadgeTone.Accent else BadgeTone.Alert,
    )
}

@Composable
fun useNavigationRail(enableFloatingBottomBar: Boolean): Boolean {
    return shouldShowSplitPane() && !(LocalUiMode.current == UiMode.Miuix && enableFloatingBottomBar)
}

@Composable
fun BottomBar(
    blurBackdrop: LayerBackdrop?,
    backdrop: Backdrop,
    navigationBadge: NavigationBadgeState,
    destinations: List<MainNavigationDestination>,
    pageCount: Int,
    fullFeatured: Boolean,
    modifier: Modifier = Modifier,
) {
    when (LocalUiMode.current) {
        UiMode.Miuix -> BottomBarMiuix(
            blurBackdrop = blurBackdrop,
            backdrop = backdrop,
            navigationBadge = navigationBadge,
            destinations = destinations,
            pageCount = pageCount,
            fullFeatured = fullFeatured,
            modifier = modifier,
        )
        UiMode.Material -> BottomBarMaterial(
            navigationBadge = navigationBadge,
            destinations = destinations,
            pageCount = pageCount,
            fullFeatured = fullFeatured,
        )
    }
}

@Composable
fun SideRail(
    navigationBadge: NavigationBadgeState,
    destinations: List<MainNavigationDestination>,
    pageCount: Int,
    fullFeatured: Boolean,
    expanded: Boolean,
    onExpandedChanged: (Boolean) -> Unit,
    modifier: Modifier = Modifier,
) {
    when (LocalUiMode.current) {
        UiMode.Miuix -> NavigationRailMiuix(
            navigationBadge = navigationBadge,
            destinations = destinations,
            pageCount = pageCount,
            fullFeatured = fullFeatured,
            expanded = expanded,
            onExpandedChanged = onExpandedChanged,
            modifier = modifier,
        )
        UiMode.Material -> NavigationRailMaterial(
            navigationBadge = navigationBadge,
            destinations = destinations,
            pageCount = pageCount,
            fullFeatured = fullFeatured,
            expanded = expanded,
            onExpandedChanged = onExpandedChanged,
            modifier = modifier,
        )
    }
}
