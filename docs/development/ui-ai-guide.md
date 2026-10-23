    # GhostLock UI · AI 生成指南

- 日期：2026-10-21
- 状态：AI 生成 UI 的默认规范
- 快照：HEAD `d2778a1a`；范围 `app/src/main/kotlin/com/ghostlock/app/ui/component/**`、`ui/kit/**` 与四个根页面
- 原则：**共享状态 + 双套件实现 + 语义 facade**。禁止复制控件、禁止只写一套、禁止使用未接线 `LocalUiKit`。

---

## 1. 先记住三件事

1. **生产入口是 `UiMode` + `LocalUiMode`**，不是 `UiKit` / `LocalUiKit`。
2. 根页面必须同时提供 Material/Miuix 两个实现：`XScreen.kt` + `XMaterial.kt` + `XMiuix.kt`。
3. 共享代码（Screen、UiState、Actions、通用组件）不得 import 具体 Material3/Miuix 控件；主题控件只在对应实现文件里出现。

```text
MainActivity
  └─ LocalUiMode
       ├─ XScreen.kt        // 只装配 ViewModel/UiState/Actions + when
       ├─ XMaterial.kt      // Material3 / Expressive
       └─ XMiuix.kt         // Miuix
```

`LocalUiKit` 当前没有 Provider，调用会直接 `error("UiKit not provided")`。AI 新代码不得使用：

```text
LocalUiKit
MaterialKit
MiuixKit
AndroidUiKitStore / UiKitController（除非完成全仓迁移）
```

---

## 2. 组件清单

### 2.1 通用 UI

| 组件 | 用途 | 位置 |
|---|---|---|
| `AppIconImage` | 应用图标，按 mode 调色 | `ui/component/AppIconImage.kt` |
| `KeyEventBlocker` | 拦截按键 | `ui/component/KeyEventBlocker.kt` |
| `KsuValidCheck` | 根状态校验包装 | `ui/component/KsuValidCheck.kt` |
| `ObserveAsEvents` | Flow → 一次性事件 | `ui/component/ObserveAsEvents.kt` |
| `ScrollToTopOnChange` | 列表变更回顶 | `ui/component/ScrollToTop.kt` |
| `SearchStatus` | 搜索栏展开/收起状态 | `ui/component/SearchStatus.kt` |
| `WarningLevel` | WarningCard 的语义等级 | `ui/component/WarningLevel.kt` |
| `MenuPositionProvider` / `ListPopupDefaults` | 菜单锚点 | `ui/component/MenuPositionProvider.kt` |

### 2.2 语义 facade

这些是**页面应该调用的入口**，不要绕过 facade 直接选实现：

| facade | Material | Miuix | 用途 |
|---|---|---|---|
| `StatusTag` | `StatusTagMaterial` | `StatusTagMiuix` | 短状态、版本 tag |
| `Dialog` | `DialogMaterial` | `DialogMiuix` | 确认/加载/错误 |
| `ProfileConfig` | `*Material` | `*Miuix` | App/Root/Template 表单 |
| `BottomBar` / `SideRail` | `BottomBarMaterial` / `NavigationRailMaterial` | `BottomBarMiuix` / `NavigationRailMiuix` | 根页面导航 |
| `MarkdownContent` | 共用 | 共用 | 不受信任 Markdown |

### 2.3 Material / Miuix 对应

| 语义 | Material | Miuix |
|---|---|---|
| 页面骨架 | `ExpressiveScaffold` | `Scaffold` |
| 顶栏 | `LargeFlexibleTopAppBar` / `TopAppBar` | `TopAppBar` / `SmallTopAppBar` |
| 设置分组 | `SegmentedColumn` | `Card` + preference |
| 普通行 | `SegmentedListItem` | `BasicComponent` / `ArrowPreference` |
| 开关 | `SegmentedSwitchItem` | `SwitchPreference` |
| 下拉 | `SegmentedDropdownItem` | `OverlayDropdownPreference` |
| 内容卡 | `TonalCard` | `Card` |
| 对话框 | `ExpressiveDialog` / `ConfirmDialogMaterial` | `WindowDialog` / `ConfirmDialogMiuix` |
| 搜索 | `SearchAppBar` | `SearchPager` / `SearchBox` |
| 输入 | `SegmentedTextField` | `EditText` |
| 警告 | **当前无公共 Material 实现** | `WarningCard` |

**重要：** 仓库没有 `SegmentedList()` 函数。实际入口是：

```text
SegmentedColumn
SegmentedListItem
SegmentedSwitchItem
SegmentedDropdownItem
SegmentedRadioItem
SegmentedCheckboxItem
SegmentedTextField
```

---

## 3. 控件选择决策树

```text
新增 UI
├─ ui/component 已有同语义 facade？
│  ├─ 是 → 直接复用，不复制
│  └─ 否 → 继续
├─ 是页面级结构？
│  ├─ 是 → XScreen + XMaterial + XMiuix + UiState/Actions
│  └─ 否 → 继续
├─ 需要两套外观？
│  ├─ 是 → 建立 facade + FooMaterial/FooMiuix
│  └─ 否 → 仅内部效果允许单套件
└─ 放 ui/component/<domain>/；screen 只调用 facade
```

### 3.1 什么时候用什么

- **设置/表单**：`SegmentedColumn` + `Segmented*Item`；不要用 `TonalCard` 包整组设置。
- **单独内容/CTA/更新卡**：`TonalCard`；不要嵌套整组设置。
- **确认/危险操作**：`rememberConfirmDialog`；不要手写 `AlertDialog`/`WindowDialog`。
- **加载**：`rememberLoadingDialog`；不要全屏手写 Dialog。
- **短状态**：`StatusTag`，必须有文字，不能只靠颜色。
- **错误/提示**：优先公共 `WarningCard`；当前 Material 私有实现应抽公共。
- **搜索列表**：`SearchBar` facade + `SearchStatus`；不要把搜索状态塞进每个页面。
- **根导航**：`BottomBar`/`SideRail`；详情页隐藏，不要重复实现。
- **Markdown**：只用 `MarkdownContent`；不要用它渲染普通字符串。

### 3.2 反例

1. `TonalCard` 里塞 10 个手写 `Row + Switch`。
2. Miuix 页面 import `SegmentedListItem` / `TonalCard`。
3. 每个页面各写一份私有 `WarningCard`。
4. `XScreen.kt` 直接 import `MaterialTheme` 或 `MiuixTheme`。
5. 使用 `LocalUiKit.current`。
6. 调用不存在的 `SegmentedList()`。
7. 只实现 Material，默认 Miuix 用户看不到页面。
8. 硬编码 `Color(0xFF...)`、`13.dp`、中文字符串。

---

## 4. 双套件 parity

### 必须一致

- 同一个 `UiState`、同一组 `Actions`
- 同一组 `stringResource`
- 同一 loading/empty/error/disabled 语义
- 同一导航/返回/确认步骤
- 同一 `contentDescription` 与状态文字

### 允许差异

- Material 用 Expressive/Scaffold/Segmented；Miuix 用 Card/Preference
- Miuix 可有 blur/backdrop/floating bottom bar；Material 无
- 容器、圆角、动画可有套件风格差异
- **功能与状态不得差异**

### 文件结构

```text
ui/screen/<feature>/
  <Feature>Screen.kt
  <Feature>Material.kt
  <Feature>Miuix.kt
  <Feature>UiState.kt
  <Feature>Actions.kt
  <Feature>Components.kt   // 仅该页面使用
```

---

## 5. 设计思想

### 页面结构

```text
Scaffold / ExpressiveScaffold
 ├─ TopBar
 ├─ Warning / Notice（如有）
 ├─ Hero / Status（Home）
 ├─ 设置分组
 ├─ 内容卡
 ├─ 主操作
 └─ bottomInnerPadding（根 pager 传入）
```

- 页面自己不要再加底部 `navigationBars` padding。
- 顶部 insets：Material `safeDrawing.only(Top + Horizontal)`；Miuix `systemBars + displayCutout.only(Horizontal)`。
- 长内容可滚动；短设置页 `verticalScroll`，长列表 `LazyColumn`。

### 间距/圆角

```text
Spacing.xs = 4
Spacing.sm = 8
Spacing.md = 12
Spacing.lg = 16
Spacing.xl = 24
```

- 页面水平 16dp、纵向 12dp 为常用值。
- 卡片圆角用主题/组件默认；不要手写 `RoundedCornerShape(16.dp)`。
- 不要新增魔法 13/17dp。

### 颜色/字体

- 只用 `MaterialTheme.colorScheme.*` / `MiuixTheme.colorScheme.*`。
- 不要硬编码颜色。
- Material 用 `typography.*`；Miuix 用 `textStyles.*`。
- 日志字体从 `LogSettings.fontFamily()` 读取。

### 状态

| 状态 | 用法 |
|---|---|
| Loading | `rememberLoadingDialog` 或 Markdown placeholder |
| Empty | 列表空态 + 下一步动作 |
| Error | `WarningCard` / Snackbar；阻塞用确认框 |
| Status | `StatusTag` + 文字 |
| Disabled | 通过 `enabled`，不要透明遮罩 |
| In-flight | 禁用重复点击，由 UiState 驱动 |

### 可达性

- 操作/状态图标必须有 `contentDescription`；装饰图标才可为 null。
- 支持 RTL：`padding(start/end)`，不要 left/right。
- 支持字体缩放，不要固定 sp 覆盖主题。
- `DialogMiuix` 已处理 back；不要绕过。

### Preview

每个页面实现至少一个 `@Preview`，使用 fake UiState/no-op Actions；不得触发 ViewModel/IO。

---

## 6. AI 检查清单

新增页面前必须回答：

1. 根页面还是详情页？
2. `XScreen/XMaterial/XMiuix` 是否齐全？
3. 是否已有 `UiState/Actions`？
4. 是否已有同语义 `ui/component` facade？
5. 是否真的需要两套？默认需要。
6. 是否需要隐藏底栏？
7. 是否复用搜索/警告/Profile/Dialog/BottomBar？
8. 新字符串中英文是否都加？
9. loading/empty/error/disabled 四态是否齐？
10. 是否有双 Preview？

### 禁止

- 只写 Material 或只写 Miuix 根页面。
- 共享代码 import Material3/Miuix 控件。
- 使用 `LocalUiKit` / `MaterialKit` / `MiuixKit`。
- 新建第二套 UiMode/UiKit。
- 私有复制 WarningCard/StatusTag/Dialog/SearchBar/Segmented。
- `TonalCard` 包 settings，`SegmentedListItem` 做 hero。
- 手写 AlertDialog/WindowDialog。
- 硬编码字符串/颜色/字号/圆角/间距。
- Preview 里创建 ViewModel/IO。
- 只改一套的 contentDescription/错误文案/禁用态。

### 放置

```text
ui/component/<domain>/     跨 screen 复用
ui/screen/<feature>/       只被一个 screen 使用
ui/theme/                  主题/token
ui/kit/                    语义 facade；当前未接线，不新增使用
```

---

## 7. 标准模板

```kotlin
// FooScreen.kt
@Composable
fun FooPager(bottomInnerPadding: Dp, isCurrentPage: Boolean = true) {
    val vm = viewModel<FooViewModel>()
    val state by vm.uiState.collectAsStateWithLifecycle()
    val actions = FooActions(
        onRefresh = vm::refresh,
        onBack = { /* navigator.pop() */ },
    )
    when (LocalUiMode.current) {
        UiMode.Material -> FooMaterial(state, actions, bottomInnerPadding)
        UiMode.Miuix -> FooMiuix(state, actions, bottomInnerPadding)
    }
}

// FooMaterial.kt
@Composable
fun FooMaterial(state: FooUiState, actions: FooActions, bottomInnerPadding: Dp) {
    val scroll = TopAppBarDefaults.exitUntilCollapsedScrollBehavior(rememberTopAppBarState())
    ExpressiveScaffold(
        topBar = {
            LargeFlexibleTopAppBar(
                title = { Text(stringResource(R.string.foo_title)) },
                colors = expressiveTopAppBarColors(),
                scrollBehavior = scroll,
            )
        },
    ) { padding ->
        Column(
            Modifier.padding(padding).verticalScroll(rememberScrollState())
                .padding(horizontal = Spacing.lg)
                .padding(bottom = bottomInnerPadding),
            verticalArrangement = Arrangement.spacedBy(Spacing.md),
        ) {
            state.error?.let { WarningCard(it) }
            SegmentedColumn {
                state.items.forEach { item ->
                    item(key = item) {
                        SegmentedListItem(
                            headlineContent = { Text(item) },
                            onClick = { actions.onItemClick(item) },
                        )
                    }
                }
            }
        }
    }
}

// FooMiuix.kt
@Composable
fun FooMiuix(state: FooUiState, actions: FooActions, bottomInnerPadding: Dp) {
    val scroll = MiuixScrollBehavior()
    Scaffold(
        topBar = { TopAppBar(title = stringResource(R.string.foo_title), scrollBehavior = scroll) },
    ) { padding ->
        LazyColumn(
            modifier = Modifier.padding(padding),
            contentPadding = PaddingValues(
                start = Spacing.lg, end = Spacing.lg, bottom = bottomInnerPadding,
            ),
            verticalArrangement = Arrangement.spacedBy(Spacing.md),
        ) {
            state.error?.let { item { WarningCard(it) } }
            items(state.items) { item ->
                Card { BasicComponent(title = item, onClick = { actions.onItemClick(item) }) }
            }
        }
    }
}
```

---

## 8. 四个页面的现状

| 页面 | 当前入口 | 依赖 | 结论 |
|---|---|---|---|
| Home | `HomePager` + Material/Miuix | data/navigation3/attack | 双实现正确；私有 WarningCard 应抽公共 |
| Path | `BackendPager` + `BackendPage` | Backends/Finish | 入口已改名；`BackendPage.kt` 仍把 Material/Miuix 放一个文件，应拆 |
| Run | `RunPageMaterial/Miuix` 同文件 | AttackClient/engine | 应拆 RunScreen/RunMaterial/RunMiuix；状态上移 |
| Settings | `SettingPager` + Material/Miuix | data/attack | 双实现正确；Actions 顺序需统一 |

### 清理项

- `RunCoordinator 2.kt`：重复冲突文件，删除。
- `HomeMaterial` / `SulogMaterial` 私有 `WarningCard`：抽公共 facade。
- `GhostlockKit/UiKit/AndroidUiKitStore`：未接线；AI 不使用，后续决定正式接线或删除。
- `material/SendLogBottomSheet` / `miuix/SendLogDialog`：Settings 专用，不提升通用。
- `FloatingBottomBar`：Miuix 内部可选，不做 Material 对等。

---

## 9. 总判

正确骨架是：

```text
LocalUiMode
+ 共享 UiState/Actions
+ 双套件实现
+ 语义 facade
+ 主题 token
+ 双 Preview
```

**AI 默认策略：**

```text
先搜 ui/component
再写 XScreen/XMaterial/XMiuix
共享状态，不共享具体控件
不碰 LocalUiKit
不复制 WarningCard/SearchBar/Dialog
不硬编码颜色/字符串/间距
```

未验证：未跑 Compose Preview/视觉回归/无障碍/字体缩放/真机；`GhostlockKit` 的未来接线计划未确认。
