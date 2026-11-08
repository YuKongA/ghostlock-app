package com.ghostlock.app.ui.component.bottombar

import com.ghostlock.app.ui.icon.Icons as ProjectIcons

import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.systemBars
import androidx.compose.foundation.layout.union
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.WideNavigationRail
import androidx.compose.material3.WideNavigationRailDefaults
import androidx.compose.material3.WideNavigationRailItem
import androidx.compose.material3.WideNavigationRailValue
import androidx.compose.material3.rememberWideNavigationRailState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.launch
import com.ghostlock.app.R
import com.ghostlock.app.ui.LocalMainPagerState

@Composable
fun NavigationRailMaterial(
    navigationBadge: NavigationBadgeState,
    destinations: List<MainNavigationDestination>,
    pageCount: Int,
    fullFeatured: Boolean,
    expanded: Boolean,
    onExpandedChanged: (Boolean) -> Unit,
    modifier: Modifier = Modifier,
) {
    if (!fullFeatured) return

    val mainPagerState = LocalMainPagerState.current

    val items = destinations.take(pageCount)

    val state = rememberWideNavigationRailState(
        initialValue = if (expanded) {
            WideNavigationRailValue.Expanded
        } else {
            WideNavigationRailValue.Collapsed
        },
    )
    val scope = rememberCoroutineScope()
    val railExpanded = state.targetValue == WideNavigationRailValue.Expanded
    LaunchedEffect(state.targetValue) {
        onExpandedChanged(state.targetValue == WideNavigationRailValue.Expanded)
    }

    WideNavigationRail(
        modifier = modifier.fillMaxHeight(),
        state = state,
        colors = WideNavigationRailDefaults.colors().copy(
            containerColor = MaterialTheme.colorScheme.surfaceContainer,
            contentColor = MaterialTheme.colorScheme.onSurface,
        ),
        windowInsets = WindowInsets.systemBars.union(WindowInsets.displayCutout).only(
            WindowInsetsSides.Start + WindowInsetsSides.Vertical
        ),
        contentPadding = PaddingValues(vertical = 20.dp),
        header = {
            IconButton(
                modifier = Modifier.padding(start = 24.dp),
                onClick = {
                    scope.launch {
                        if (railExpanded) state.collapse() else state.expand()
                    }
                },
            ) {
                Icon(
                    if (railExpanded) ProjectIcons.AutoMirrored.Filled.MenuOpen.material else ProjectIcons.Filled.Menu.material,
                    contentDescription = stringResource(
                        if (railExpanded) R.string.nav_rail_collapse else R.string.nav_rail_expand
                    )
                )
            }
        },
    ) {
        items.forEachIndexed { index, destination ->
            val selected = mainPagerState.selectedPage == index
            WideNavigationRailItem(
                railExpanded = railExpanded,
                selected = selected,
                onClick = {
                    if (!selected) {
                        mainPagerState.animateToPage(index)
                    }
                },
                icon = {
                    NavigationIconWithBadge(
                        icon = if (selected) destination.materialSelectedIcon else destination.materialUnselectedIcon,
                        contentDescription = stringResource(destination.label),
                        badge = badgeFor(index, navigationBadge),
                    )
                },
                label = { Text(stringResource(destination.label)) }
            )
        }
    }
}
