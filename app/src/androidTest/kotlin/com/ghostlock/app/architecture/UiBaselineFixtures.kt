package com.ghostlock.app.architecture

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.pager.rememberPagerState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.key
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import com.ghostlock.app.backend.backends.cve43284.BackendCve43284
import com.ghostlock.app.backend.backends.cve43499glued.BackendCve43499Glued
import com.ghostlock.app.backend.backends.debugprobe.BackendDebugProbe
import com.ghostlock.app.backend.backends.lua.BackendCustom
import com.ghostlock.app.R
import com.ghostlock.app.ui.LocalMainPagerState
import com.ghostlock.app.ui.LocalUiMode
import com.ghostlock.app.ui.UiMode
import com.ghostlock.app.ui.backend.RegisteredBackendConfigRenderer
import com.ghostlock.app.ui.component.bottombar.rememberMainPagerState
import com.ghostlock.app.ui.component.dialog.ContentDialogMaterial
import com.ghostlock.app.ui.component.dialog.ContentDialogMiuix
import com.ghostlock.app.ui.kit.LocalUiKit
import com.ghostlock.app.ui.kit.MaterialKit
import com.ghostlock.app.ui.kit.MiuixKit
import com.ghostlock.app.ui.screen.about.AboutScreenMaterial
import com.ghostlock.app.ui.screen.about.AboutScreenMiuix
import com.ghostlock.app.ui.screen.about.AboutActions
import com.ghostlock.app.ui.screen.about.defaultAboutUiState
import com.ghostlock.app.ui.screen.colorpalette.ColorPaletteScreenActions
import com.ghostlock.app.ui.screen.colorpalette.ColorPaletteScreenMaterial
import com.ghostlock.app.ui.screen.colorpalette.ColorPaletteScreenMiuix
import com.ghostlock.app.ui.screen.colorpalette.ColorPaletteUiState
import com.ghostlock.app.ui.screen.home.HomeActions
import com.ghostlock.app.ui.screen.home.HomePagerMaterial
import com.ghostlock.app.ui.screen.home.HomePagerMiuix
import com.ghostlock.app.ui.screen.home.HomeUiState
import com.ghostlock.app.ui.screen.home.PrivilegeUiState
import com.ghostlock.app.ui.screen.home.SelectedBackendUiState
import com.ghostlock.app.ui.screen.home.SystemInfo
import com.ghostlock.app.ui.screen.module.BackendPageMaterial
import com.ghostlock.app.ui.screen.module.BackendPageMiuix
import com.ghostlock.app.ui.screen.module.BackendOptionUiState
import com.ghostlock.app.ui.screen.module.BackendPageActions
import com.ghostlock.app.ui.screen.module.BackendPageUiState
import com.ghostlock.app.ui.screen.module.backends.FinishPage
import com.ghostlock.app.ui.screen.module.backends.FinishSettingsPage
import com.ghostlock.app.ui.screen.module.backends.FinishEditorUiState
import com.ghostlock.app.ui.screen.module.backends.FinishSettingsUiState
import com.ghostlock.app.ui.screen.run.RunPageMaterial
import com.ghostlock.app.ui.screen.run.RunPageMiuix
import com.ghostlock.app.ui.screen.run.RunActions
import com.ghostlock.app.ui.screen.run.RunTerminalEntry
import com.ghostlock.app.ui.screen.run.RunTerminalLevel
import com.ghostlock.app.ui.screen.run.RunTerminalOrigin
import com.ghostlock.app.ui.screen.run.RunUiPhase
import com.ghostlock.app.ui.screen.run.RunUiState
import com.ghostlock.app.ui.screen.settings.SettingPagerMaterial
import com.ghostlock.app.ui.screen.settings.SettingPagerMiuix
import com.ghostlock.app.ui.screen.settings.SettingsScreenActions
import com.ghostlock.app.ui.screen.settings.SettingsUiState
import com.ghostlock.app.ui.theme.AppSettings
import com.ghostlock.app.ui.theme.ColorMode
import com.ghostlock.app.ui.theme.GhostLockAppTheme
import com.ghostlock.app.ui.theme.LocalColorMode
import com.ghostlock.app.ui.theme.LocalEnableBlur
import com.ghostlock.app.ui.theme.LocalEnableFloatingBottomBar
import com.ghostlock.app.ui.theme.LocalEnableFloatingBottomBarBlur
import com.ghostlock.app.domain.update.AppUpdateInfo
import com.ghostlock.app.domain.privilege.AttackExecutorKind
import com.ghostlock.app.domain.finish.FinishEditorState
import com.ghostlock.app.domain.backend.BackendConfigMutationResult
import com.ghostlock.app.domain.backend.BackendChoiceKind
import com.ghostlock.app.domain.backend.BackendChoiceOption
import com.ghostlock.app.domain.backend.BackendConfigIcon
import com.ghostlock.app.domain.backend.BackendConfigValue
import com.ghostlock.app.domain.backend.BackendConfiguration
import com.ghostlock.app.domain.backend.BackendConfigurationPort
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import com.materialkolor.PaletteStyle
import com.materialkolor.dynamiccolor.ColorSpec

/**
 * Describes one approved D0 production-composable baseline member.
 *
 * @property memberId Stable filename identity from `ui-surface-matrix-v1.tsv`.
 * @property group Matrix group used as the output directory.
 * @property surface Production surface selected by the fixture.
 * @property mode Existing page-level Material or Miuix branch.
 * @property theme Forced light or dark application theme.
 * @property layout Narrow or wide capture transaction.
 * @property semanticsRecords Exact semantics record names required by the D0 contract.
 * @property panelNames Named component-gallery assertions, empty outside M5c.
 * @note D0 only; this type supplies ordinary values and never dispatches an attack.
 */
internal data class D0SurfaceCase(
    val memberId: String,
    val group: String,
    val surface: String,
    val mode: UiMode,
    val theme: ColorMode,
    val layout: String,
    val semanticsRecords: List<String>,
    val panelNames: List<String> = emptyList(),
)

/**
 * Supplies deterministic inputs to the existing production renderers used by D0.
 *
 * @note The fixture injects inert state into the same production renderer seam and invokes no action.
 */
internal object UiBaselineFixtures {
    internal const val ROOT_TAG = "d0-root"
    internal const val DISABLED_PANEL_TAG = "d0-panel-disabled"
    internal const val EMPTY_PANEL_TAG = "d0-panel-empty"
    internal const val OVERSIZE_PANEL_TAG = "d0-panel-oversize"

    private val modes = listOf(UiMode.Material, UiMode.Miuix)
    private val themes = listOf(ColorMode.LIGHT, ColorMode.DARK)
    private val tripleRecords = listOf("normal", "rotation", "back")
    private val normalRecord = listOf("normal")
    private val galleryPanels = listOf("disabled", "empty", "oversize")

    /**
     * Expands the approved active D0 matrix for one layout transaction.
     *
     * @param layout `narrow` for M1/M2/M4/M5b/M5c or `wide` for M3.
     * @return Exactly 52 narrow members or 16 wide members.
     * @throws IllegalArgumentException When the layout is outside the approved matrix.
     * @note The 20 M5a rows remain deferred to D5 and are never returned here.
     */
    fun activeCases(layout: String): List<D0SurfaceCase> = when (layout) {
        "narrow" -> buildList {
            addSurfaceGroup("M1", listOf("home", "backend", "run_idle", "settings"), layout, tripleRecords)
            addSurfaceGroup("M2", listOf("about", "color_palette", "finish", "finish_settings"), layout, tripleRecords)
            addSurfaceGroup("M4", listOf("config_43499", "config_43284", "config_debug"), layout, normalRecord)
            addSurfaceGroup("M5b", listOf("confirm_dialog"), layout, normalRecord)
            addSurfaceGroup("M5c", listOf("component_gallery"), layout, normalRecord, galleryPanels)
        }

        "wide" -> buildList {
            addSurfaceGroup("M3", listOf("home", "backend", "run_idle", "settings"), layout, normalRecord)
        }

        else -> throw IllegalArgumentException("unsupported D0 layout: $layout")
    }

    /**
     * Returns the six non-dispatch menu surfaces used by the A301SO M5 walk.
     *
     * @return Home, backend, idle run, settings, finish, and finish-settings fixtures.
     * @note The caller renders each surface but performs no click or run action.
     */
    fun deviceWalkMenus(): List<D0SurfaceCase> = listOf(
        deviceWalkCase("home"),
        deviceWalkCase("backend"),
        deviceWalkCase("run_idle"),
        deviceWalkCase("settings"),
        deviceWalkCase("finish"),
        deviceWalkCase("finish_settings"),
    )

    /**
     * Returns the four standard semantic ConfigCard renderers used by the device walk.
     *
     * @return ChoiceDocument, Toggle, Static, and Document cases.
     * @note The cards receive no interaction and cannot dispatch an attack.
     */
    fun deviceWalkConfigCards(): List<D0SurfaceCase> = listOf(
        deviceWalkCase("config_43499"),
        deviceWalkCase("config_43284"),
        deviceWalkCase("config_debug"),
        deviceWalkCase("config_custom"),
    )

    /**
     * Renders one matrix member through the existing theme and page-level branch.
     *
     * @param fixture Approved member and ordinary input values.
     * @return Unit after composing the production renderer.
     * @note Missing surface classification fails before capture rather than substituting a fallback.
     */
    @Composable
    fun Render(fixture: D0SurfaceCase) {
        val pagerState = rememberPagerState(initialPage = 0, pageCount = { 4 })
        val mainPagerState = rememberMainPagerState(pagerState = pagerState, animatePageChanges = false)
        val kit = if (fixture.mode == UiMode.Material) MaterialKit else MiuixKit
        val settings = AppSettings(
            colorMode = fixture.theme,
            keyColor = 0,
            paletteStyle = PaletteStyle.TonalSpot,
            colorSpec = ColorSpec.SpecVersion.SPEC_2025,
        )

        CompositionLocalProvider(
            LocalUiMode provides fixture.mode,
            LocalColorMode provides fixture.theme.value,
            LocalEnableBlur provides false,
            LocalEnableFloatingBottomBar provides false,
            LocalEnableFloatingBottomBarBlur provides false,
            LocalMainPagerState provides mainPagerState,
            LocalUiKit provides kit,
        ) {
            GhostLockAppTheme(appSettings = settings, uiMode = fixture.mode) {
                key(fixture.memberId) {
                    Box(Modifier.fillMaxSize().testTag(ROOT_TAG)) {
                        RenderSurface(fixture)
                    }
                }
            }
        }
    }

    /**
     * Expands a matrix group into both UI modes and both forced themes.
     *
     * @param group Approved group identifier.
     * @param surfaces Approved production surface names.
     * @param layout Narrow or wide layout identity.
     * @param records Semantics record names for every generated member.
     * @param panels Optional M5c named panel assertions.
     * @return Unit after appending members to the receiver list.
     * @note Member identifiers match the machine manifest naming rule exactly.
     */
    private fun MutableList<D0SurfaceCase>.addSurfaceGroup(
        group: String,
        surfaces: List<String>,
        layout: String,
        records: List<String>,
        panels: List<String> = emptyList(),
    ) {
        for (surface in surfaces) {
            for (mode in modes) {
                for (theme in themes) {
                    val state = if (group == "M5c") "disabled_empty_oversize" else "normal"
                    val memberSurface = when (surface) {
                        "run_idle" -> "run_idle"
                        else -> surface
                    }
                    val memberId = buildString {
                        append(group.lowercase())
                        append('_')
                        append(memberSurface)
                        append('_')
                        append(state)
                        append('_')
                        append(mode.value)
                        append('_')
                        append(theme.name.lowercase())
                        append('_')
                        append(layout)
                    }
                    add(
                        D0SurfaceCase(
                            memberId = memberId,
                            group = group.lowercase(),
                            surface = surface,
                            mode = mode,
                            theme = theme,
                            layout = layout,
                            semanticsRecords = records,
                            panelNames = panels,
                        ),
                    )
                }
            }
        }
    }

    /**
     * Creates one stable Miuix/light member for the non-interactive device walk.
     *
     * @param surface Production menu or ConfigCard surface.
     * @return A device-walk-only fixture identity.
     * @note This identity is not written into the golden archive.
     */
    private fun deviceWalkCase(surface: String): D0SurfaceCase = D0SurfaceCase(
        memberId = "device_walk_$surface",
        group = "device-walk",
        surface = surface,
        mode = UiMode.Miuix,
        theme = ColorMode.LIGHT,
        layout = "device",
        semanticsRecords = normalRecord,
    )

    /**
     * Selects the existing production composable for one approved surface.
     *
     * @param fixture Surface identity and page-level UI branch.
     * @return Unit after composing the selected renderer.
     * @throws IllegalArgumentException When the surface is not classified by the D0 matrix.
     * @note Run is rendered only in its production idle state and receives no interaction.
     */
    @Composable
    private fun RenderSurface(fixture: D0SurfaceCase) {
        val miuix = fixture.mode == UiMode.Miuix
        when (fixture.surface) {
            "home" -> if (miuix) {
                HomePagerMiuix(homeState(), homeActions(), 0.dp)
            } else {
                HomePagerMaterial(homeState(), homeActions(), 0.dp)
            }

            "backend" -> if (miuix) {
                BackendPageMiuix(
                    state = backendState(),
                    actions = backendActions(),
                    finishEditorState = finishEditorState(),
                    finishSettingsState = FinishSettingsUiState(),
                    configRenderer = backendConfigRenderer,
                    bottomInnerPadding = 0.dp,
                )
            } else {
                BackendPageMaterial(
                    state = backendState(),
                    actions = backendActions(),
                    finishEditorState = finishEditorState(),
                    finishSettingsState = FinishSettingsUiState(),
                    configRenderer = backendConfigRenderer,
                    bottomInnerPadding = 0.dp,
                )
            }
            "run_idle" -> if (miuix) {
                RunPageMiuix(runIdleState(), runActions(), 0.dp)
            } else {
                RunPageMaterial(runIdleState(), runActions(), 0.dp)
            }
            "settings" -> if (miuix) {
                SettingPagerMiuix(settingsState(fixture), settingsActions(), 0.dp)
            } else {
                SettingPagerMaterial(settingsState(fixture), settingsActions(), 0.dp)
            }

            "about" -> if (miuix) {
                AboutScreenMiuix(defaultAboutUiState(), aboutActions())
            } else {
                AboutScreenMaterial(defaultAboutUiState(), aboutActions())
            }
            "color_palette" -> if (miuix) {
                ColorPaletteScreenMiuix(colorPaletteState(fixture), colorPaletteActions())
            } else {
                ColorPaletteScreenMaterial(colorPaletteState(fixture), colorPaletteActions())
            }

            "finish" -> FinishPage(bottomInnerPadding = 0.dp, onBack = {})
            "finish_settings" -> FinishSettingsPage(bottomInnerPadding = 0.dp, onBack = {})
            "config_43499" -> CardFrame { backendConfigRenderer.render(BackendCve43499Glued.key) }
            "config_43284" -> CardFrame { backendConfigRenderer.render(BackendCve43284.key) }
            "config_debug" -> CardFrame { backendConfigRenderer.render(BackendDebugProbe.key) }
            "config_custom" -> CardFrame { backendConfigRenderer.render(BackendCustom.key) }
            "confirm_dialog" -> ConfirmDialog(miuix)
            "component_gallery" -> ComponentGallery()
            else -> throw IllegalArgumentException("unclassified D0 surface: ${fixture.surface}")
        }
    }

    /**
     * Provides a stable not-ready backend state to the production home page.
     *
     * @return Ordinary D0 values without legacy manager state.
     * @note Dynamic timestamps, host facts, and network update data are excluded.
     */
    private fun homeState(): HomeUiState = HomeUiState(
        selectedBackend = SelectedBackendUiState(
            cveLabel = "CVE-2026-43499",
            statusText = "Not ready",
            isReady = false,
        ),
        isPrBuild = false,
        checkUpdateEnabled = false,
        latestVersionInfo = AppUpdateInfo(),
        currentAppVersionCode = 0,
        systemInfo = SystemInfo(
            kernelVersion = "6.1.0-d0",
            appVersion = "3-d0",
            deviceModel = "Pixel_5",
            fingerprint = "d0-production-baseline",
            selinuxStatus = "Enforcing",
            seccompStatus = 2,
        ),
        privilege = PrivilegeUiState(
            selectedExecutor = AttackExecutorKind.APP,
            displayedExecutor = AttackExecutorKind.APP,
            appUid = 10231,
            shizukuAvailable = false,
            shizukuGranted = false,
        ),
    )

    /** @return Inert About-page callbacks for the baseline renderer. */
    private fun aboutActions(): AboutActions = AboutActions(onBack = {}, onOpenUrl = {})

    private val backendConfigRenderer = RegisteredBackendConfigRenderer(
        object : BackendConfigurationPort {
            override val states: StateFlow<Map<String, BackendConfiguration>> = MutableStateFlow(
                mapOf(
                    BackendCve43499Glued.key to BackendConfiguration.ChoiceDocument(
                        key = BackendCve43499Glued.key,
                        field = "profile_reference",
                        title = "Profile",
                        summary = "Required",
                        options = listOf(BackendChoiceOption(null, "None", BackendChoiceKind.NONE)),
                        selectedIndex = 0,
                        importTitle = "Import",
                        importSummary = "Select a profile document",
                        mimeTypes = listOf("text/plain"),
                        errorText = "Invalid profile",
                        invalid = false,
                    ),
                    BackendCve43284.key to BackendConfiguration.Toggle(
                        key = BackendCve43284.key,
                        field = "kernel_access_after_relay",
                        title = "Kernel access",
                        enabled = false,
                        enabledSummary = "On",
                        disabledSummary = "Off",
                        icon = BackendConfigIcon.SECURITY,
                    ),
                    BackendDebugProbe.key to BackendConfiguration.Static(
                        key = BackendDebugProbe.key,
                        values = listOf(BackendConfigValue("ID", "Debug Probe")),
                    ),
                    BackendCustom.key to BackendConfiguration.Document(
                        key = BackendCustom.key,
                        field = "script_uri",
                        importTitle = "Import Lua script",
                        documentSummary = "No script selected",
                        mimeTypes = listOf("text/x-lua", "text/plain"),
                        facts = listOf(BackendConfigValue("Runtime", "Disabled")),
                        details = null,
                    ),
                ),
            )

            override fun refresh(key: String) = Unit

            override fun setBoolean(
                key: String,
                field: String,
                value: Boolean,
            ): BackendConfigMutationResult = BackendConfigMutationResult.APPLIED

            override fun selectValue(
                key: String,
                field: String,
                value: String?,
            ): BackendConfigMutationResult = BackendConfigMutationResult.APPLIED

            override fun importDocument(
                key: String,
                field: String,
                uri: String,
            ): BackendConfigMutationResult = BackendConfigMutationResult.APPLIED
        },
    )

    /** @return Deterministic blocked-idle state rendered without obtaining a real session adapter. */
    private fun runIdleState(): RunUiState = RunUiState(
        backendKey = "d0-backend",
        backendLabel = "CVE-2026-D0",
        startAllowed = false,
        blockedReasons = listOf("D0 capture does not dispatch"),
        phase = RunUiPhase.Idle,
        entries = listOf(
            RunTerminalEntry(RunTerminalOrigin.Kotlin, RunTerminalLevel.Info, "ghostlock terminal"),
            RunTerminalEntry(RunTerminalOrigin.Kotlin, RunTerminalLevel.Info, "waiting for execution"),
        ),
    )

    /** @return Inert semantic callbacks proving the capture does not dispatch or mutate a session. */
    private fun runActions(): RunActions = RunActions(
        onPrimaryAction = {},
        onClearTerminal = {},
        onShareTerminal = {},
        onConfirmRerun = {},
        onDismissRerun = {},
    )

    /** @return Stable selector state for both production path-page skins. */
    private fun backendState(): BackendPageUiState = BackendPageUiState(
        options = listOf(
            BackendOptionUiState(BackendCve43499Glued.key, "CVE-2026-43499-Glued", true),
            BackendOptionUiState(BackendCve43284.key, "CVE-2026-43284", BackendCve43284.selectable),
            BackendOptionUiState(BackendDebugProbe.key, "Debug Probe", BackendDebugProbe.selectable),
        ),
        selectedKey = BackendCve43499Glued.key,
    )

    /** @return Empty deterministic finish-editor state for path-page summary rendering. */
    private fun finishEditorState(): FinishEditorUiState = FinishEditorUiState(
        editor = FinishEditorState(
            rootManager = "",
            safeMode = false,
            payloads = emptyList(),
            rootManagers = emptyList(),
        ),
    )

    /** @return No-op interactions for deterministic path-page rendering. */
    private fun backendActions(): BackendPageActions = BackendPageActions(
        onSelectBackend = {},
        onOpenFinish = {},
        onOpenFinishSettings = {},
    )

    /**
     * Supplies inert home callbacks.
     *
     * @return Callbacks that do not mutate state, navigate, or dispatch.
     * @note No callback is invoked by the capture test.
     */
    private fun homeActions(): HomeActions = HomeActions(
        onOpenUrl = {},
        onOpenPrivilege = {},
    )

    /**
     * Creates deterministic settings values for the selected UI mode and theme.
     *
     * @param fixture Current matrix member.
     * @return Settings state consumed by the production renderer.
     * @note Blur and floating chrome are disabled to remove device-specific animation variance.
     */
    private fun settingsState(fixture: D0SurfaceCase): SettingsUiState = SettingsUiState(
        uiMode = fixture.mode.value,
        themeMode = fixture.theme.value,
        enableBlur = false,
        enableFloatingBottomBar = false,
        enableFloatingBottomBarBlur = false,
    )

    /**
     * Supplies inert settings callbacks.
     *
     * @return Complete no-op action contract.
     * @note Capture never opens a route or changes a preference.
     */
    private fun settingsActions(): SettingsScreenActions = SettingsScreenActions(
        onSetCheckUpdate = {},
        onOpenTheme = {},
        onSetUiModeIndex = {},
        onOpenAbout = {},
        onSetLogLevelIndex = {},
        onSelectTerminalFont = {},
        onSetShowLogTimestamp = {},
        onSetAutoExportLogs = {},
        onSelectLogExportDirectory = { true },
        onSetWarnOnRerun = {},
        onSetAutoAttackOnBoot = {},
        onSetBootRetryCount = {},
        createDiagnosticReport = { java.io.File("/dev/null") },
    )

    /**
     * Creates the palette-page state from the same approved mode and theme.
     *
     * @param fixture Current matrix member.
     * @return Stable palette state with TonalSpot and SPEC_2025.
     * @note No system Monet input is read.
     */
    private fun colorPaletteState(fixture: D0SurfaceCase): ColorPaletteUiState = ColorPaletteUiState(
        uiState = settingsState(fixture),
        currentColorMode = fixture.theme,
        currentPaletteStyle = PaletteStyle.TonalSpot,
        currentColorSpec = ColorSpec.SpecVersion.SPEC_2025,
    )

    /**
     * Supplies inert color-palette callbacks.
     *
     * @return Complete no-op action contract.
     * @note Capture does not persist or navigate.
     */
    private fun colorPaletteActions(): ColorPaletteScreenActions = ColorPaletteScreenActions(
        onBack = {},
        onSetThemeMode = {},
        onSetMiuixMonet = {},
        onSetKeyColor = {},
        onSetColorMode = {},
        onSetColorStyle = {},
        onSetColorSpec = {},
        onSetEnableBlur = {},
        onSetEnableFloatingBottomBar = {},
        onSetEnableFloatingBottomBarBlur = {},
        onSetEnableNavigationBadge = {},
        onSetEnablePredictiveBack = {},
        onSetEnableSwipeDismiss = {},
        onSetPagerInterceptionMode = {},
        onSetPageScale = {},
    )

    /**
     * Gives a standalone ConfigCard the same bounded viewport in both UI modes.
     *
     * @param content Existing production ConfigCard content.
     * @return Unit after composing the card inside a scrollable page frame.
     * @note The frame adds no business behavior.
     */
    @Composable
    private fun CardFrame(content: @Composable () -> Unit) {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .padding(16.dp),
        ) {
            content()
        }
    }

    /**
     * Renders the existing mode-specific confirmation dialog shell.
     *
     * @param miuix True for Miuix, false for Material.
     * @return Unit after composing the production dialog.
     * @note The dismiss callback is inert and the test performs no click.
     */
    @Composable
    private fun ConfirmDialog(miuix: Boolean) {
        if (miuix) {
            ContentDialogMiuix(title = "Confirm", onDismissRequest = {}) {
                Text("D0 production dialog")
            }
        } else {
            ContentDialogMaterial(onDismissRequest = {}, title = "Confirm") {
                Text("D0 production dialog")
            }
        }
    }

    /**
     * Renders one semantic-component gallery with disabled, empty, and oversize panels.
     *
     * @return Unit after composing all three named panels through `LocalUiKit`.
     * @note Panel tags are assertions only; appearance remains owned by MaterialKit or MiuixKit.
     */
    @Composable
    private fun ComponentGallery() {
        val kit = LocalUiKit.current
        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
            kit.Screen("D0 component gallery") {
                Box(Modifier.testTag(DISABLED_PANEL_TAG)) {
                    kit.Section("Disabled") {
                        kit.PrimaryButton(text = "Disabled", enabled = false, onClick = {})
                    }
                }
                Box(Modifier.testTag(EMPTY_PANEL_TAG)) {
                    kit.Section("Empty") {
                        kit.Body(text = "", modifier = Modifier)
                    }
                }
                Box(Modifier.testTag(OVERSIZE_PANEL_TAG)) {
                    kit.Section("Oversize") {
                        kit.Body(text = "oversize ".repeat(80), modifier = Modifier)
                    }
                }
            }
        }
    }
}
