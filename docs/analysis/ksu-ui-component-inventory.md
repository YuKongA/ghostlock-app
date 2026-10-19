> **状态（2026-10-18）：KernelSU 框架已清空，本清单仅作历史记录；GhostLock 的界面见 `ui/gl/` 与 `ui/kit/`。**

# KernelSU 移植 UI 控件清单（分析记录）

> 2026-10-18。目的：在清空 KernelSU 框架、换成 GhostLock 自己的控件之前，记录**上游控件怎么用**：
> 每个组件文件包了哪些 miuix / Material3 原语、暴露了什么语义、被多少处界面引用。

## 1. 组件清单（69 个文件）

| 文件 | 组合函数 | miuix 原语 | M3 原语 | 被引用文件数 |
|---|---|---|---|---|
| `component/AppIconImage.kt` | AppIconImage, AppIconImage | MiuixTheme | MaterialTheme | 8 |
| `component/FloatingBottomBar.kt` | FloatingBottomBar | Backdrop, blur, drawBackdrop, BloomStroke, Highlight, LightPosition | — | 0 |
| `component/KeyEventBlocker.kt` | KeyEventBlocker | — | — | 0 |
| `component/KsuValidCheck.kt` | KsuIsValid | — | — | 0 |
| `component/MenuPositionProvider.kt` | — | PopupPositionProvider | — | 0 |
| `component/ObserveAsEvents.kt` | — | — | — | 0 |
| `component/ScrollToTop.kt` | ScrollToTopOnChange | — | — | 6 |
| `component/SearchStatus.kt` | — | colorScheme | — | 0 |
| `component/WarningLevel.kt` | — | — | — | 0 |
| `component/bottombar/BottomBar.kt` | rememberMainPagerState, useNavigationRail, BottomBar, SideRail | Backdrop, LayerBackdrop, springAnimateToPage | — | 2 |
| `component/bottombar/BottomBarMaterial.kt` | BottomBarMaterial | — | Badge, BadgedBox, Icon, MaterialTheme, ShortNavigationBar, ShortNavigationBarItem | 0 |
| `component/bottombar/BottomBarMiuix.kt` | BottomBarMiuix | Badge, BadgedBox, Icon, NavigationBar, NavigationBarItem, NavigationItem | — | 0 |
| `component/bottombar/NavigationRailMaterial.kt` | NavigationRailMaterial | — | Icon, IconButton, MaterialTheme, Text, WideNavigationRail, WideNavigationRailDefaults | 0 |
| `component/bottombar/NavigationRailMiuix.kt` | NavigationRailMiuix | NavigationRail, NavigationRailItem, NavigationRailValue, rememberNavigationRailState, MiuixTheme | — | 0 |
| `component/dialog/Dialog.kt` | rememberLoadingDialog, rememberConfirmCallback, rememberConfirmDialog, rememberConfirmDialog | — | — | 16 |
| `component/dialog/DialogMaterial.kt` | LoadingDialogMaterial, ConfirmDialogMaterial | — | LoadingIndicator, MaterialTheme, Surface, Text, TextButton | 0 |
| `component/dialog/DialogMiuix.kt` | LoadingDialogMiuix, ConfirmDialogMiuix | ButtonDefaults, InfiniteProgressIndicator, Text, TextButton, LocalDismissState, MiuixTheme | — | 0 |
| `component/filter/BaseFieldFilter.kt` | — | — | — | 0 |
| `component/filter/FilterNumber.kt` | — | — | — | 0 |
| `component/liquid/CombinedBackdrop.kt` | rememberCombinedBackdrop | Backdrop | — | 0 |
| `component/liquid/InnerShadow.kt` | — | — | — | 0 |
| `component/liquid/Lens.kt` | — | BackdropEffectScope, isRuntimeShaderSupported, runtimeShaderEffect | — | 0 |
| `component/liquid/Vibrancy.kt` | — | BackdropEffectScope, colorControls | — | 0 |
| `component/markdown/GithubMarkdown.kt` | GithubMarkdown | MiuixTheme | MaterialTheme | 0 |
| `component/markdown/MarkdownContent.kt` | MarkdownContent | InfiniteProgressIndicator | LoadingIndicator, MaterialTheme | 0 |
| `component/material/ExpressiveDialog.kt` | ExpressiveDialog, ExpressiveConfirmDialog | — | AlertDialog, AlertDialogDefaults, MaterialTheme, Text, TextButton | 0 |
| `component/material/ExpressiveMenu.kt` | OffsetAnchoredExpressiveMenu | — | DropdownMenuGroup, DropdownMenuPopup, MenuDefaults | 0 |
| `component/material/ExpressiveScaffold.kt` | ExpressiveScaffold, expressiveTopAppBarColors | — | FabPosition, MaterialTheme, Scaffold, ScaffoldDefaults, TopAppBarColors, TopAppBarDefaults | 21 |
| `component/material/ExpressiveSwitch.kt` | ExpressiveSwitch, expressiveSwitchColors | — | Icon, MaterialTheme, Switch, SwitchColors, SwitchDefaults | 1 |
| `component/material/ExpressiveTabRow.kt` | ExpressiveTabRow | — | LocalContentColor, MaterialTheme, PrimaryTabRow, Text, ripple | 0 |
| `component/material/ExpressiveToggleButton.kt` | ExpressiveToggleButton, expressiveToggleButtonColors | — | MaterialTheme, ToggleButton, ToggleButtonColors, ToggleButtonDefaults, ToggleButtonShapes | 2 |
| `component/material/SearchBar.kt` | SearchAppBar | — | ExpandedFullScreenContainedSearchBar, Icon, IconButton, IconButtonDefaults, LargeFlexibleTopAppBar, MaterialTheme | 3 |
| `component/material/SegmentedList.kt` | SegmentedColumn, SegmentedColumn, SegmentedItem, SegmentedItemContainer, SegmentedListItem, SegmentedListItem, SegmentedListItem, SegmentedSwitchItem, SegmentedDropdownItem, SegmentedRadioItem, SegmentedCheckboxItem, SegmentedTextField | — | Checkbox, SelectableDropdownMenuItem, Icon, ListItemColors, ListItemDefaults, ListItemShapes | 55 |
| `component/material/SendLogBottomSheet.kt` | SendLogBottomSheet | — | FilledTonalIconButton, Icon, MaterialTheme, ModalBottomSheet, SheetValue, SnackbarHostState | 1 |
| `component/material/SnackBar.kt` | SnackBarHost | — | Snackbar, SnackbarData, SnackbarHost, SnackbarHostState, SwipeToDismissBox, SwipeToDismissBoxValue | 5 |
| `component/material/TonalCard.kt` | TonalCard | — | Card, CardDefaults, MaterialTheme, contentColorFor | 5 |
| `component/material/TopBarBackButton.kt` | TopBarBackButton | — | Icon, IconButton, IconButtonDefaults, MaterialTheme | 8 |
| `component/miuix/DropdownItem.kt` | DropdownItem | DropdownColors, DropdownDefaults, Text, MiuixTheme | — | 4 |
| `component/miuix/EditText.kt` | EditText | Text, MiuixTheme, colorScheme | — | 2 |
| `component/miuix/ScaleDialog.kt` | ScaleDialog | ButtonDefaults, Text, TextButton, TextField, OverlayDialog, colorScheme | — | 1 |
| `component/miuix/SendLogDialog.kt` | SendLogDialog | Icon, Text, TextButton, OverlayDialog, ArrowPreference, MiuixTheme | — | 1 |
| `component/miuix/SuperEditArrow.kt` | SuperEditArrow | BasicComponentColors, BasicComponentDefaults, ButtonDefaults, TextButton, TextField, OverlayDialog | — | 0 |
| `component/miuix/SuperSearchBar.kt` | SearchBar, SearchBarFake | Icon, InputField, Text, MiuixIcons, Search, SearchCleanup | — | 3 |
| `component/miuix/WarningCard.kt` | WarningCard | Card, CardDefaults, Text, colorScheme, isDynamicColor, PressFeedbackType | — | 4 |
| `component/miuix/animation/DampedDragAnimation.kt` | — | — | — | 0 |
| `component/miuix/animation/InteractiveHighlight.kt` | — | — | — | 0 |
| `component/miuix/effect/BgEffectBackground.kt` | BgEffectBackground | isRuntimeShaderSupported, MiuixTheme | — | 1 |
| `component/miuix/effect/BgEffectConfig.kt` | — | — | — | 0 |
| `component/miuix/effect/BgEffectModifier.kt` | — | — | — | 0 |
| `component/miuix/effect/BgEffectPainter.kt` | — | RuntimeShader, asBrush | — | 0 |
| `component/miuix/effect/ColorBlendToken.kt` | — | BlendColorEntry, BlurBlendMode | — | 0 |
| `component/miuix/effect/DeviceType.kt` | — | — | — | 0 |
| `component/miuix/effect/OS3BgFrag.kt` | — | — | — | 0 |
| `component/miuix/modifier/DragGestureInspector.kt` | — | — | — | 0 |
| `component/profile/AppProfileConfigMaterial.kt` | AppProfileConfigMaterial | — | — | 0 |
| `component/profile/AppProfileConfigMiuix.kt` | AppProfileConfigMiuix | SwitchPreference | — | 0 |
| `component/profile/ProfileConfig.kt` | AppProfileConfig, RootProfileConfig, TemplateConfig | — | — | 8 |
| `component/profile/RootProfileConfigMaterial.kt` | RootProfileConfigMaterial | — | AlertDialog, OutlinedTextField, Text, TextButton | 0 |
| `component/profile/RootProfileConfigMiuix.kt` | RootProfileConfigMiuix | ButtonDefaults, TextButton, TextField, TextFieldDefaults, OverlayDialog, ArrowPreference | — | 0 |
| `component/profile/TemplateConfigMaterial.kt` | TemplateConfigMaterial | — | Icon, IconButton, Text | 0 |
| `component/profile/TemplateConfigMiuix.kt` | TemplateConfigMiuix | Icon, ArrowPreference, OverlayDropdownPreference, MiuixTheme | — | 0 |
| `component/profile/dialogs/MultiSelectDialog.kt` | — | — | Icon, IconButton, ListItemDefaults, MaterialTheme, ModalBottomSheet, OutlinedTextField | 0 |
| `component/profile/dialogs/SingleSelectDialog.kt` | — | — | AlertDialog, Text, TextButton | 0 |
| `component/rebootlistpopup/RebootListPopup.kt` | getRebootListOption, rememberRebootAction, RebootListPopup | — | — | 1 |
| `component/rebootlistpopup/RebootListPopupMaterial.kt` | RebootDropdownItems, RebootListPopupMaterial | — | DropdownMenuGroup, SelectableDropdownMenuItem, DropdownMenuPopup, Icon, IconButton, MenuDefaults | 0 |
| `component/rebootlistpopup/RebootListPopupMiuix.kt` | RebootListPopupMiuix, RebootDropdownItem | Icon, IconButton, ListPopupColumn, PopupPositionProvider, MiuixIcons, Close2 | — | 1 |
| `component/statustag/StatusTag.kt` | StatusTag | — | — | 10 |
| `component/statustag/StatusTagMaterial.kt` | StatusTagMaterial | — | MaterialTheme, Text | 0 |
| `component/statustag/StatusTagMiuix.kt` | StatusTagMiuix | Text | — | 0 |

## 2. 界面清单（56 个文件）

- `screen/about/AboutMaterial.kt`
- `screen/about/AboutMiuix.kt`
- `screen/about/AboutScreen.kt`
- `screen/about/AboutUiState.kt`
- `screen/about/AboutUtils.kt`
- `screen/appprofile/AppProfileMaterial.kt`
- `screen/appprofile/AppProfileMiuix.kt`
- `screen/appprofile/AppProfileScreen.kt`
- `screen/appprofile/AppProfileUiState.kt`
- `screen/appprofile/AppProfileUtils.kt`
- `screen/colorpalette/ColorPaletteScreen.kt`
- `screen/colorpalette/ColorPaletteScreenMaterial.kt`
- `screen/colorpalette/ColorPaletteScreenMiuix.kt`
- `screen/colorpalette/ColorPaletteUiState.kt`
- `screen/executemoduleaction/ExecuteModuleActionMaterial.kt`
- `screen/executemoduleaction/ExecuteModuleActionMiuix.kt`
- `screen/executemoduleaction/ExecuteModuleActionScreen.kt`
- `screen/executemoduleaction/ExecuteModuleActionUiState.kt`
- `screen/executemoduleaction/ExecuteModuleActionUtils.kt`
- `screen/home/HomeMaterial.kt`
- `screen/home/HomeMiuix.kt`
- `screen/home/HomeScreen.kt`
- `screen/home/HomeUiState.kt`
- `screen/home/HomeUtils.kt`
- `screen/module/ModuleMaterial.kt`
- `screen/module/ModuleMiuix.kt`
- `screen/module/ModulePreinit.kt`
- `screen/module/ModuleScreen.kt`
- `screen/module/ModuleShortcutState.kt`
- `screen/module/ModuleUiState.kt`
- `screen/module/ModuleUtils.kt`
- `screen/scriptrun/ScriptRunMaterial.kt`
- `screen/scriptrun/ScriptRunMiuix.kt`
- `screen/scriptrun/ScriptRunScreen.kt`
- `screen/scriptrun/ScriptRunUiState.kt`
- `screen/settings/SettingsMaterial.kt`
- `screen/settings/SettingsMiuix.kt`
- `screen/settings/SettingsScreen.kt`
- `screen/settings/SettingsUiState.kt`
- `screen/sulog/SulogMaterial.kt`
- `screen/sulog/SulogMiuix.kt`
- `screen/sulog/SulogScreen.kt`
- `screen/sulog/SulogUiState.kt`
- `screen/superuser/SuperUserMaterial.kt`
- `screen/superuser/SuperUserMiuix.kt`
- `screen/superuser/SuperUserScreen.kt`
- `screen/superuser/SuperUserUiState.kt`
- `screen/template/TemplateMaterial.kt`
- `screen/template/TemplateMiuix.kt`
- `screen/template/TemplateScreen.kt`
- `screen/template/TemplateUiState.kt`
- `screen/templateeditor/TemplateEditorMaterial.kt`
- `screen/templateeditor/TemplateEditorMiuix.kt`
- `screen/templateeditor/TemplateEditorScreen.kt`
- `screen/templateeditor/TemplateEditorUiState.kt`
- `screen/templateeditor/TemplateEditorUtils.kt`

## 3. 值得抄的用法（GhostLock 控件按这些来）

1. **语义先行**：上游把“设置项”拆成 `SegmentedSwitchItem` / `SegmentedDropdownItem` /
   `SegmentedRadioItem` / `SegmentedTextField`（见 `component/material/SegmentedList.kt`），
   内部才落到 M3 的 `ListItem` + `Switch` / `DropdownMenu` / `RadioButton`。
   GhostLock 只需要 `SwitchItem` / `DropdownItem` / `RadioItem` / `TextFieldItem` 这一层语义。
2. **状态卡**：`HomeMaterial` 用 `TonalCard`（`component/material/TonalCard.kt`）承载
   “Working/Failed” 大状态 + 详情行；对应 GhostLock 的 `Hero(title, subtitle, active)`。
3. **双套件**：每个屏幕都有 `*Material.kt` 与 `*Miuix.kt` 两份实现，共享同一个 `ViewModel`；
   壳层用 `LocalUiMode` 分派（`UiMode.kt`）。GhostLock 用 `LocalUiKit` 做同一件事。
4. **底栏/侧栏**：`BottomBar` 按 `useNavigationRail()` 在 `NavigationBar` 与
   `NavigationRail` 间切换；悬浮底栏（`FloatingBottomBar`）是可选皮，默认关闭。
5. **对话框**：`rememberConfirmDialog / rememberLoadingDialog` 返回“状态 + 渲染函数”，
   调用方只描述内容，不关心 Material/miuix 差异（`component/dialog/Dialog.kt`）。
6. **顶栏**：`ExpressiveScaffold` + `TopBarBackButton` 提供统一返回键与滚动行为；
   GhostLock 只需要 `TopBar(title, actions)`。
7. **间距/字号**：上游直接用 M3 typography token（`MaterialTheme.typography.*`）与
   miuix `MiuixTheme.textStyles.*`，没有自定义字号表 —— GhostLock 沿用同一策略。

## 4. 结论（给 GhostLock 控件层）

GhostLock 只需要 9 个语义组件即可覆盖已规划的四个页面：

```text
TopBar(title, actions)                 Screen(title, content)
Hero(title, subtitle, active)          Section(title, content)
SwitchItem / ArrowItem / DropdownItem / RadioItem
PrimaryButton / SecondaryButton / TextAction
Navigation(items, selected, onSelect)  Page(topBar, bottomBar, content)
```

其余（SearchBar、SegmentedList 全家族、Markdown、WebUI、悬浮底栏、liquid blur）属于 KernelSU 的
功能型组件，GhostLock 清空框架时一并删除。
