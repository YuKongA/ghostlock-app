package com.ghostlock.app.ui

import android.annotation.SuppressLint
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.BackHandler
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.viewModels
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.animation.core.tween
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.consumeWindowInsets
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.systemBars
import androidx.compose.foundation.layout.union
import androidx.compose.foundation.pager.HorizontalPager
import androidx.compose.foundation.pager.PagerDefaults.flingBehavior
import androidx.compose.foundation.pager.PagerDefaults.pageNestedScrollConnection
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.tooling.preview.Preview
import androidx.core.splashscreen.SplashScreen.Companion.installSplashScreen
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.lifecycleScope
import androidx.navigationevent.NavigationEventInfo
import androidx.navigationevent.compose.NavigationBackHandler
import androidx.navigationevent.compose.rememberNavigationEventState
import com.ghostlock.app.GhostLockApp
import com.ghostlock.app.ui.component.bottombar.BottomBar
import com.ghostlock.app.ui.component.bottombar.MainNavigationDestination
import com.ghostlock.app.ui.component.bottombar.MainPagerState
import com.ghostlock.app.ui.component.bottombar.NavigationBadgeState
import com.ghostlock.app.ui.component.bottombar.SideRail
import com.ghostlock.app.ui.component.bottombar.rememberMainPagerState
import com.ghostlock.app.ui.component.bottombar.useNavigationRail
import com.ghostlock.app.ui.navigation3.LocalNavigator
import com.ghostlock.app.ui.navigation3.Navigator
import com.ghostlock.app.ui.navigation3.Route
import com.ghostlock.app.ui.navigation3.rememberNavigator
import com.ghostlock.app.ui.kit.LocalUiKit
import com.ghostlock.app.ui.kit.MaterialKit
import com.ghostlock.app.ui.kit.MiuixKit
import com.ghostlock.app.ui.screen.about.AboutScreen
import com.ghostlock.app.ui.screen.colorpalette.ColorPaletteScreen
import com.ghostlock.app.ui.screen.home.HomePager
import com.ghostlock.app.ui.screen.module.BackendPager
import com.ghostlock.app.ui.screen.settings.SettingPager
import com.ghostlock.app.ui.theme.GhostLockAppTheme
import com.ghostlock.app.ui.theme.AppSettings
import com.ghostlock.app.ui.theme.ColorMode
import com.materialkolor.PaletteStyle
import com.materialkolor.dynamiccolor.ColorSpec
import com.ghostlock.app.ui.theme.LocalColorMode
import com.ghostlock.app.ui.theme.LocalEnableBlur
import com.ghostlock.app.ui.theme.LocalEnableFloatingBottomBar
import com.ghostlock.app.ui.theme.LocalEnableFloatingBottomBarBlur
import com.ghostlock.app.ui.util.rememberBlurBackdrop
import com.ghostlock.app.ui.util.rememberContentReady
import com.ghostlock.app.ui.viewmodel.MainActivityViewModel
import com.ghostlock.app.ui.viewmodel.MainPagerConfig
import top.yukonga.miuix.kmp.basic.Scaffold
import top.yukonga.miuix.kmp.blur.layerBackdrop
import top.yukonga.miuix.kmp.blur.rememberLayerBackdrop
import top.yukonga.miuix.kmp.nav.core.NavDisplay
import top.yukonga.miuix.kmp.nav.core.NavDisplayEffects
import top.yukonga.miuix.kmp.nav.core.rememberNavSystemCornerRadius
import top.yukonga.miuix.kmp.nav.transition.NavSwipeDirection
import top.yukonga.miuix.kmp.theme.MiuixTheme
import top.yukonga.miuix.kmp.utils.PagerGestureNestedScrollConnection
import top.yukonga.miuix.kmp.utils.PagerInterceptionMode
import top.yukonga.miuix.kmp.utils.PagerNavigationSpringSpec
import top.yukonga.miuix.kmp.utils.pagerGestureOverride
import kotlinx.coroutines.launch

class MainActivity : ComponentActivity() {

    private var contentReady = false
    private val mainActivityViewModel by viewModels<MainActivityViewModel> {
        (application as GhostLockApp).container.viewModelFactory
    }


    @SuppressLint("UnusedMaterial3ScaffoldPaddingParameter")
    override fun onCreate(savedInstanceState: Bundle?) {
        val splashScreen = installSplashScreen()
        super.onCreate(savedInstanceState)
        /* 图标动画已移除（2026-10-19）：只等首帧内容，不再固定停留 500ms。 */
        splashScreen.setKeepOnScreenCondition { !contentReady }

        lifecycleScope.launch {
            try {
                (application as GhostLockApp).container.initialize()
                if (isFinishing) return@launch
                setContent {
            val viewModel = mainActivityViewModel
            val uiState by viewModel.uiState.collectAsStateWithLifecycle()
            val selectedMainPage by viewModel.selectedMainPage.collectAsStateWithLifecycle()
            val appSettings = uiState.appSettings
            val uiMode = uiState.uiMode
            val darkMode = appSettings.colorMode.isDark || (appSettings.colorMode.isSystem && isSystemInDarkTheme())

            DisposableEffect(darkMode) {
                enableEdgeToEdge(
                    statusBarStyle = SystemBarStyle.auto(
                        android.graphics.Color.TRANSPARENT,
                        android.graphics.Color.TRANSPARENT
                    ) { darkMode },
                    navigationBarStyle = SystemBarStyle.auto(
                        android.graphics.Color.TRANSPARENT,
                        android.graphics.Color.TRANSPARENT
                    ) { darkMode },
                )
                window.isNavigationBarContrastEnforced = false
                onDispose { }
            }

            val navigator = rememberNavigator(Route.Main)
            val systemDensity = LocalDensity.current
            val density = remember(systemDensity, uiState.pageScale) {
                Density(systemDensity.density * uiState.pageScale, systemDensity.fontScale)
            }

            CompositionLocalProvider(
                LocalNavigator provides navigator,
                LocalDensity provides density,
                LocalColorMode provides appSettings.colorMode.value,
                LocalEnableBlur provides uiState.enableBlur,
                LocalEnableFloatingBottomBar provides uiState.enableFloatingBottomBar,
                LocalEnableFloatingBottomBarBlur provides uiState.enableFloatingBottomBarBlur,
                LocalUiMode provides uiMode,
                LocalUiKit provides if (uiMode == UiMode.Miuix) MiuixKit else MaterialKit,
            ) {
                GhostLockAppTheme(appSettings = appSettings, uiMode = uiMode) {
                    val swipeDismiss = if (uiState.enableSwipeDismiss) {
                        if (LocalLayoutDirection.current == androidx.compose.ui.unit.LayoutDirection.Rtl) {
                            NavSwipeDirection.RightToLeft
                        } else {
                            NavSwipeDirection.LeftToRight
                        }
                    } else {
                        NavSwipeDirection.None
                    }
                    val mainScreenEntry = @Composable {
                        MainScreen(
                            initialPage = selectedMainPage,
                            pagerInterceptionMode = uiState.pagerInterceptionMode,
                            navigationRailExpanded = uiState.navigationRailExpanded,
                            onPageChanged = viewModel::setSelectedMainPage,
                            onNavigationRailExpandedChanged = viewModel::updateNavigationRailExpanded,
                        )
                    }

                    val navDisplay = @Composable {
                        NavDisplay(
                            backStack = navigator.backStack,
                            effects = NavDisplayEffects(cornerClipRadius = rememberNavSystemCornerRadius()),
                            onBack = { navigator.pop() },
                        ) {
                            entry<Route.Main>(swipeDismiss = swipeDismiss) { mainScreenEntry() }
                            entry<Route.About>(swipeDismiss = swipeDismiss) { AboutScreen() }
                            entry<Route.ColorPalette>(swipeDismiss = swipeDismiss) { ColorPaletteScreen() }
                        }
                    }

                    when (uiMode) {
                        UiMode.Material -> androidx.compose.material3.Scaffold(
                            containerColor = MaterialTheme.colorScheme.surfaceContainer
                        ) { navDisplay() }

                        UiMode.Miuix -> Scaffold { navDisplay() }
                    }
                    SideEffect { contentReady = true }
                }
            }
                }
            } catch (problem: Throwable) {
                android.util.Log.e("GhostLockStartup", "application initialization failed", problem)
                contentReady = true
                finish()
            }
        }
    }

}

val LocalMainPagerState = staticCompositionLocalOf<MainPagerState> { error("LocalMainPagerState not provided") }

/** Main-screen-owned full-page overlay; no process-global Compose state participates. */
private enum class MainOverlay {
    NONE,
    PAYLOAD,
    FINISH_SETTINGS,
}

@SuppressLint("UnusedMaterial3ScaffoldPaddingParameter")
@Composable
fun MainScreen(
    initialPage: Int = 0,
    pagerInterceptionMode: Int = PagerInterceptionMode.CrossAxisInterceptor.ordinal,
    navigationRailExpanded: Boolean = false,
    onPageChanged: (Int) -> Unit = {},
    onNavigationRailExpandedChanged: (Boolean) -> Unit = {},
) {
    val navController = LocalNavigator.current
    val enableBlur = LocalEnableBlur.current
    val enableFloatingBottomBar = LocalEnableFloatingBottomBar.current
    val enableFloatingBottomBarBlur = LocalEnableFloatingBottomBarBlur.current
    val useNavigationRail = useNavigationRail(enableFloatingBottomBar)
    val pagerState = rememberPagerState(initialPage = initialPage, pageCount = { MainPagerConfig.PAGE_COUNT })
    val mainPagerState = rememberMainPagerState(
        pagerState = pagerState,
        animatePageChanges = !useNavigationRail,
    )
    val pagerMode = PagerInterceptionMode.entries.getOrElse(pagerInterceptionMode) {
        PagerInterceptionMode.Native
    }
    val interceptPagerGestures = pagerMode == PagerInterceptionMode.CrossAxisInterceptor
    var userScrollEnabled by remember { mutableStateOf(true) }
    var overlay by rememberSaveable { mutableStateOf(MainOverlay.NONE) }

    /* 当前四页固定为主页/路径/执行/设置，不再预载已删除的 manager 页面。 */
    val navigationBadge = NavigationBadgeState()
    val navigationDestinations = MainNavigationDestination.entries
    val uiMode = LocalUiMode.current
    val surfaceColor = when (uiMode) {
        UiMode.Material -> MaterialTheme.colorScheme.surface // Blur is not used in Material, this is just a placeholder
        UiMode.Miuix -> MiuixTheme.colorScheme.surface
    }
    val blurBackdrop = rememberBlurBackdrop(enableBlur)

    val backdrop = rememberLayerBackdrop {
        drawRect(surfaceColor)
        drawContent()
    }

    val settledPage = mainPagerState.pagerState.settledPage
    LaunchedEffect(settledPage) {
        onPageChanged(settledPage)
    }

    val currentPage = mainPagerState.pagerState.currentPage
    LaunchedEffect(currentPage) {
        mainPagerState.syncPage()
    }

    MainScreenBackHandler(mainPagerState, navController)

    CompositionLocalProvider(
        LocalMainPagerState provides mainPagerState
    ) {
        val contentReady = rememberContentReady()
        val pagerContent = @Composable { bottomInnerPadding: Dp ->
            Box(modifier = if (blurBackdrop != null) Modifier.layerBackdrop(blurBackdrop) else Modifier) {
                HorizontalPager(
                    modifier = Modifier
                        .pagerGestureOverride(
                            pagerState = mainPagerState.pagerState,
                            mode = pagerMode,
                            enabled = userScrollEnabled,
                        )
                        .then(if (enableFloatingBottomBar && enableFloatingBottomBarBlur) Modifier.layerBackdrop(backdrop) else Modifier),
                    state = mainPagerState.pagerState,
                    beyondViewportPageCount = if (contentReady) 3 else 0,
                    overscrollEffect = null,
                    userScrollEnabled = userScrollEnabled && !interceptPagerGestures,
                    pageNestedScrollConnection = if (interceptPagerGestures) {
                        PagerGestureNestedScrollConnection
                    } else {
                        pageNestedScrollConnection(
                            state = mainPagerState.pagerState,
                            orientation = androidx.compose.foundation.gestures.Orientation.Horizontal,
                        )
                    },
                    flingBehavior = flingBehavior(
                        state = mainPagerState.pagerState,
                        snapAnimationSpec = PagerNavigationSpringSpec,
                    ),
                ) { page ->
                    val isCurrentPage = page == mainPagerState.selectedPage
                    when (page) {
                        0 -> if (contentReady || isCurrentPage) HomePager(navController, bottomInnerPadding, isCurrentPage)
                        /* 2026-10-18 用户裁定：路径页与执行页互换位置（路径在前）。 */
                        1 -> if (contentReady || isCurrentPage) {
                            BackendPager(
                                bottomInnerPadding = bottomInnerPadding,
                                isCurrentPage = isCurrentPage,
                                onOpenPayload = { overlay = MainOverlay.PAYLOAD },
                                onOpenFinishSettings = { overlay = MainOverlay.FINISH_SETTINGS },
                            )
                        }
                        2 -> if (contentReady || isCurrentPage) {
                            com.ghostlock.app.ui.screen.run.RunPage(
                                bottomInnerPadding = bottomInnerPadding,
                                isCurrentPage = isCurrentPage,
                            )
                        }
                        3 -> if (contentReady || isCurrentPage) SettingPager(navController, bottomInnerPadding, isCurrentPage)
                    }
                }
            }
        }

        Box(modifier = Modifier.fillMaxSize()) {
            if (useNavigationRail) {
                val startInsets = WindowInsets.systemBars.union(WindowInsets.displayCutout)
                    .only(WindowInsetsSides.Start)
                val navBarBottomPadding = WindowInsets.systemBars.asPaddingValues().calculateBottomPadding()

                when (uiMode) {
                    UiMode.Material -> androidx.compose.material3.Scaffold(
                        containerColor = MaterialTheme.colorScheme.surfaceContainer
                    ) {
                        Row {
                            SideRail(
                                navigationBadge = navigationBadge,
                                destinations = navigationDestinations,
                                pageCount = MainPagerConfig.PAGE_COUNT,
                                fullFeatured = true,
                                expanded = navigationRailExpanded,
                                onExpandedChanged = onNavigationRailExpandedChanged,
                            )
                            Box(
                                modifier = Modifier
                                    .weight(1f)
                                    .consumeWindowInsets(startInsets)
                            ) {
                                pagerContent(navBarBottomPadding)
                            }
                        }
                    }

                    UiMode.Miuix -> Scaffold { _ ->
                        Row {
                            SideRail(
                                navigationBadge = navigationBadge,
                                destinations = navigationDestinations,
                                pageCount = MainPagerConfig.PAGE_COUNT,
                                fullFeatured = true,
                                expanded = navigationRailExpanded,
                                onExpandedChanged = onNavigationRailExpandedChanged,
                            )
                            Box(
                                modifier = Modifier
                                    .weight(1f)
                                    .consumeWindowInsets(startInsets)
                            ) {
                                pagerContent(navBarBottomPadding)
                            }
                        }
                    }
                }
            } else {
                val bottomBar = @Composable {
                    /* 子页面（收尾等）隐藏底栏（2026-10-18 用户裁定）。 */
                    if (overlay == MainOverlay.NONE) {
                        Box(modifier = Modifier.fillMaxWidth()) {
                            BottomBar(
                                blurBackdrop = blurBackdrop,
                                backdrop = backdrop,
                                navigationBadge = navigationBadge,
                                destinations = navigationDestinations,
                                pageCount = MainPagerConfig.PAGE_COUNT,
                                fullFeatured = true,
                                modifier = Modifier.align(Alignment.BottomCenter),
                            )
                        }
                    }
                }

                when (uiMode) {
                    UiMode.Material -> androidx.compose.material3.Scaffold(
                        bottomBar = bottomBar,
                        containerColor = MaterialTheme.colorScheme.surfaceContainer
                    ) { innerPadding ->
                        pagerContent(innerPadding.calculateBottomPadding())
                    }

                    UiMode.Miuix -> Scaffold(bottomBar = bottomBar) { innerPadding ->
                        pagerContent(innerPadding.calculateBottomPadding())
                    }
                }
            }

            MainPageOverlays(
                overlay = overlay,
                onClose = { overlay = MainOverlay.NONE },
            )
        }
    }
}

/**
 * 在手机底栏与宽屏侧栏布局之上统一承载全屏子页面。
 *
 * @note 两个页面互斥；设置页优先，避免同时注册两个返回处理器。
 */
@Composable
private fun MainPageOverlays(
    overlay: MainOverlay,
    onClose: () -> Unit,
) {
    FullScreenPageOverlay(
        visible = overlay == MainOverlay.PAYLOAD,
        onBack = onClose,
    ) {
        com.ghostlock.app.ui.screen.module.payload.PayloadPage(
            bottomInnerPadding = 0.dp,
            onBack = onClose,
        )
    }
    FullScreenPageOverlay(
        visible = overlay == MainOverlay.FINISH_SETTINGS,
        onBack = onClose,
    ) {
        com.ghostlock.app.ui.screen.module.finish.FinishSettingsPage(
            bottomInnerPadding = 0.dp,
            onBack = onClose,
        )
    }
}

/**
 * 以统一动画和返回语义显示覆盖 pager 的全屏页面。
 *
 * @param visible 页面是否可见。
 * @param onBack 返回操作。
 * @param content 页面内容。
 */
@Composable
private fun FullScreenPageOverlay(
    visible: Boolean,
    onBack: () -> Unit,
    content: @Composable () -> Unit,
) {
    AnimatedVisibility(
        visible = visible,
        enter = slideInHorizontally(animationSpec = tween(260)) { it } +
            fadeIn(animationSpec = tween(200)),
        exit = slideOutHorizontally(animationSpec = tween(240)) { it } +
            fadeOut(animationSpec = tween(180)),
    ) {
        BackHandler(enabled = visible, onBack = onBack)
        content()
    }
}


@Composable
private fun MainScreenBackHandler(
    mainState: MainPagerState,
    navController: Navigator,
) {
    val isPagerBackHandlerEnabled by remember {
        derivedStateOf {
            navController.current() is Route.Main && navController.backStackSize() == 1 && mainState.selectedPage != 0
        }
    }

    val navEventState = rememberNavigationEventState(NavigationEventInfo.None)

    NavigationBackHandler(
        state = navEventState,
        isBackEnabled = isPagerBackHandlerEnabled,
        onBackCompleted = {
            mainState.animateToPage(0)
        }
    )
}

/**
 * 在不启动导航、ViewModel 或执行层的条件下预览主界面的 Material 外壳。
 *
 * @note 预览只验证主题与基础容器；完整页面仍由运行时的 [MainScreen] 组合。
 */
@Preview(name = "Main shell", showBackground = true, showSystemUi = true)
@Composable
private fun MainActivityPreview() {
    val uiMode = UiMode.Material
    val appSettings = AppSettings(
        colorMode = ColorMode.LIGHT,
        keyColor = 0,
        paletteStyle = PaletteStyle.TonalSpot,
        colorSpec = ColorSpec.SpecVersion.SPEC_2025,
    )
    CompositionLocalProvider(
        LocalUiMode provides uiMode,
        LocalUiKit provides MaterialKit,
    ) {
        GhostLockAppTheme(appSettings = appSettings, uiMode = uiMode) {
            Surface(modifier = Modifier.fillMaxSize()) {
                Box(contentAlignment = Alignment.Center) {
                    Text(text = "GhostLock")
                }
            }
        }
    }
}
