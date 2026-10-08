# Compose UI 工程规范：分组列表项形状 + 禁止手调间距

> **元信息**
> - 来源：调研任务（用户直接指令），2026-10-06。
> - 写范围：**仅新增本文件**。未改动 `app/src/main/**`、`app/src/test/**`、`app/build.gradle.kts`、`profile-core/**`、`src/core/**`；未跑 Gradle。
> - 证据基线（行号仅作定位提示，这些文件正被其他 writer 修改）：
>   - `app/src/main/kotlin/com/ghostlock/app/ui/GhostlockUI.kt` @ `4e0ff25a174c7d7ee95528b8bba401a0e5b9c002`
>   - `app/src/main/kotlin/com/ghostlock/app/ui/AdvancedUI.kt` @ `d34b42d4d05b533367ed9bc9803aaeaeb7176a43`
>   - Miuix **0.9.4** 源码（从本机 Gradle 缓存 `~/.gradle/caches/modules-2/files-2.1/top.yukonga.miuix.kmp/**-0.9.4-sources.jar` 解出，下称「Miuix 源码」）⇒ 与 `app/build.gradle.kts` 声明的 `0.9.4` **同版本**，是本次调研的一手证据。
> - 术语：**组（group）** = 视觉上连成一块的一串行；**组内位置** = 首项 / 中间项 / 末项 / 单项。

---

## 1. 结论先行（TL;DR）

1. **「末项上两角直角、下两角圆角」不是给每一项设 shape，而是「一个组 = 一个容器」。**
   组的四角由**容器**统一决定，组内的行**没有自己的容器/背景/圆角**；组内行与行之间因此天然无间隙。Miuix 的写法是 `Card { row; row; row }`（`Card` 的 content 本身就是 `ColumnScope`，`insideMargin` 默认 `PaddingValues(0.dp)`）。
   **本项目现在的写法（一行一个 `Card`）是错的**，它同时造成「上下全圆角」和「相邻行之间的异常间隙」（两个圆角矩形相接 ⇒ 相接处出现两个圆角缺口）。

2. **若行本身必须带背景（无法用单容器），才需要 per-position shape**，规则是：
   首项只圆 `topStart/topEnd`；中间项四角全直角；末项只圆 `bottomStart/bottomEnd`；`count == 1` 时四角全圆。
   - **androidx 官方 API**：`ListItemDefaults.segmentedShapes(index, count, defaultShapes)` + `SegmentedListItem(shapes = ...)`；组间距 token 是 `ListItemDefaults.SegmentedGap`。
   - **Miuix 没有等价 API**（`Card` 只有单一 `cornerRadius`），但有 per-corner 原语 `Modifier.squircleSurface(color, topStart, topEnd, bottomEnd, bottomStart)` / `Modifier.absoluteSquircleSurface(topLeft, topRight, bottomRight, bottomLeft)`，可手工表达同一规则。

3. **「禁止手调间距」不能用「移植 Miuix」来实现——Miuix 根本没有全局 spacing token。**
   Miuix 官方主题文档明说：主题系统由 **color schemes 和 text styles** 组成；`MiuixTheme` 只提供 `colorScheme` / `textStyles` / `colorSchemeMode` / `isDynamicColor`，**没有 `shapes`、没有 `dimens`/`spacing`**。Miuix 的 token 表面是**每个组件各自的 `XxxDefaults` 对象**（`CardDefaults.CornerRadius`、`BasicComponentDefaults.InsideMargin`、`DropdownDefaults.FirstLastVerticalPadding`…）。
   **实测：KernelSU 自己的 Miuix 界面里仍然手写 dp**（`HomeMiuix.kt` 30 个数字 `.dp`、`ModuleMiuix.kt` 69 个、`SuperUserMiuix.kt` 36 个）。⇒ 这条硬规则**必须由本项目自己加一层 token + 守卫测试**才能落地，不能靠「照抄 KernelSU」。

4. **Robolectric 下用像素断言形状：可行，但有三个前提。**
   compose ui-test **1.12.1**（本项目版本）的 `captureToImage()` **已经有 Robolectric 专门分支**：检测到 Robolectric 指纹时**跳过 forceRedraw**（该强制重绘在 Robolectric 里会挂起，即 2023 年 issue #8071 的病因），改走同步的 `PixelCopy` shadow。前提：`@GraphicsMode(GraphicsMode.Mode.NATIVE)`、`sdk >= 26`（`PixelCopy` 是 API 26）、`unitTests.isIncludeAndroidResources = true`（本项目**已开**）。
   ⚠️ **弹窗/popup 内的节点会走 `processMultiWindowScreenshot`（UiAutomation），Robolectric 下不可用** ⇒ 断言目标必须是**主窗口内**的 composable。

5. **最小改法（建议，未实施）**：把 `ExecutionCombinationDialog` 里「每个 leaf 一个 `Card`」改成**每组一个 `Card`**（`itemsIndexed` → 单 `item` + 组内 `forEach`）；顺手把 `Card(modifier = ...padding(top = 12.dp))` 一类手写间距收敛到 token 层。详见 §5、§6。

---

## 2. 规范条目

### R1 分组列表项的形状：容器负责四角，行不负责

**R1.1（强制）** 一个视觉分组 = **一个** shaped 容器。组内所有行**不得**有各自的容器、背景、圆角或阴影。
来源：[M3 Lists](https://m3.material.io/components/lists/overview)；[M3 Menus](https://m3.material.io/components/menus/overview)；Miuix [Card 文档](https://compose-miuix-ui.github.io/miuix/components/card.html)；KernelSU 实例 [HomeMiuix.kt L443-L470](https://github.com/tiann/KernelSU/blob/main/manager/app/src/main/java/me/weishu/kernelsu/ui/screen/home/HomeMiuix.kt#L443-L470)：

```kotlin
Card(modifier = modifier) {      // 一个组 = 一个 Card
    ArrowPreference(...)          // 行 1：没有自己的容器
    ArrowPreference(...)          // 行 2：没有自己的容器
}
```

**R1.2（强制）** 组内行之间**不得**有间距。分组之间的间距属于**分组容器之间**的间距，由 spacing token 提供，不由行提供。
Miuix 依据：`CardDefaults.InsideMargin = PaddingValues(0.dp)`（Miuix 源码 `basic/Card.kt` L195），所以 `Card { rows }` 天然无内边距、无行间隙。

**R1.3（强制）** 只要**行自带背景**，就必须按位置给 shape。四角规则（**唯一正确写法**）：

| 位置 | topStart | topEnd | bottomStart | bottomEnd |
| --- | --- | --- | --- | --- |
| 首项（count>1, index==0） | 组圆角 | 组圆角 | 0 | 0 |
| 中间项 | 0 | 0 | 0 | 0 |
| 末项（count>1, index==count-1） | 0 | 0 | 组圆角 | 组圆角 |
| 单项（count==1） | 组圆角 | 组圆角 | 组圆角 | 组圆角 |

**androidx 官方 API（推荐范式）**——`ListItemDefaults.segmentedShapes(index, count, defaultShapes)` 就是这个规则的可执行定义（源码见 §3.1，L439-L505：`count == 1` 取四角；`index == 0` 只取 `overrideShape.topStart/topEnd`；`index == count - 1` 只取 `bottomStart/bottomEnd`；其余保持 `defaultShapes` 的方角）：

```kotlin
// androidx.compose.material3（API 名已核实：SegmentedListItem 的 shapes 是必填参数）
SegmentedListItem(
    selected = selected,
    onClick = onClick,
    shapes = ListItemDefaults.segmentedShapes(index = index, count = items.size),
    content = { Text(title) },
)
// 组与组之间的间隙也用 token，不写 dp：
// ListItemDefaults.SegmentedGap
```
来源：[ListItemDefaults.kt（androidx-main）](https://android.googlesource.com/platform/frameworks/support/+/refs/heads/androidx-main/compose/material3/material3/src/commonMain/kotlin/androidx/compose/material3/ListItemDefaults.kt)；[ListItemDefaults API 参考](https://developer.android.com/reference/kotlin/androidx/compose/material3/ListItemDefaults)。

**Miuix 的等价写法（本项目实际要用的）**——Miuix 无 per-position API，用 per-corner 原语：

```kotlin
// Miuix 源码 miuix-squircle/.../SquircleBackground.kt L152-L165
// 形参顺序 = (color, topStart, topEnd, bottomEnd, bottomStart) —— 注意 bottomEnd 在 bottomStart 之前
@Composable
fun Modifier.squircleSurface(
    color: Color,
    topStart: Dp, topEnd: Dp, bottomEnd: Dp, bottomStart: Dp,
    extension: Float = SquircleDefaults.Extension,
): Modifier

// 位置 -> 四角（半径一律取 token，不写字面量）
val radius = CardDefaults.CornerRadius        // = 16.dp，Miuix 的组圆角 token
val topRounded = isFirst || isSingle
val bottomRounded = isLast || isSingle
Row(
    modifier = Modifier.squircleSurface(
        color = MiuixTheme.colorScheme.surfaceContainer,
        topStart = if (topRounded) radius else 0.dp,
        topEnd = if (topRounded) radius else 0.dp,
        bottomEnd = if (bottomRounded) radius else 0.dp,
        bottomStart = if (bottomRounded) radius else 0.dp,
    ),
) { /* row content */ }
```
- 需要**物理角**（不被 RTL 翻转）时用 `Modifier.absoluteSquircleSurface(topLeft, topRight, bottomRight, bottomLeft)`（Miuix 源码 L233-L246）。
- 只有 fill、不裁剪子树用 `squircleBackground`；只裁剪不填充用 `squircleClip`。
- **平台回退**：shader 版 squircle 需要 **Android API 33+**，低版本自动回退成 `RoundedCornerShape(...)`（Miuix 官方 squircle 文档 + 源码 L159-L164）⇒ **像素测试在 API 33 以下看到的是圆角而非连续角**，断言要按「圆/直角」而非具体轮廓写。

**R1.4（强制）** 分组容器的圆角半径**必须**来自 token（Miuix: `CardDefaults.CornerRadius`），不得在调用点写 `16.dp`。

### R2 间距与尺寸必须来自设计系统 token

**R2.1（强制）** UI 代码中**禁止**数字 `.dp` / `.sp` 字面量（token 定义文件除外）。
依据：Material 3 本身把间距纳入了 token 体系（[M3 Spacing tokens](https://m3.material.io/styles/spacing/tokens)）；Compose 官方要求设计系统把样式集中在主题/设计系统层（[Custom design systems in Compose](https://developer.android.com/develop/ui/compose/designsystems/custom)、[Anatomy of a theme in Compose](https://developer.android.com/develop/ui/compose/designsystems/anatomy)）；Google 的设计 token 概念见 [Design tokens](https://m3.material.io/foundations/design-tokens/overview)。

**R2.2（强制，优先级最高）** **能不传参数就不传**。Miuix 每个组件都带 `XxxDefaults`，直接使用默认值即为「零 dp 写法」。这是本项目最省事、最合规的做法。
例：`Card {}`（不传 `cornerRadius`/`insideMargin`）、`OverlayDialog {}`（不传 `outsideMargin`/`insideMargin`/`cornerRadius`）、`SmallTitle(text)`。

**R2.3（强制）** 需要**新的**间距时，加到本项目自己的 token 层，而不是就地写 dp。推荐形态（与 Compose 官方 CompositionLocal 机制一致，[CompositionLocal 指南](https://developer.android.com/develop/ui/compose/compositionlocal)）：

```kotlin
// 建议新增：app/src/main/kotlin/com/ghostlock/app/ui/theme/GhostlockSpacing.kt（尚未创建）
@Immutable
data class GhostlockSpacing(
    val pageHorizontal: Dp,
    val sectionGap: Dp,
    val itemHorizontal: Dp,
)

// 只用固定值的 token 用 staticCompositionLocalOf（官方：static 版不追踪读取、性能更好；
// 值会频繁变化才用 compositionLocalOf）
val LocalSpacing = staticCompositionLocalOf { GhostlockSpacing(...) }

// 提供方：MiuixTheme 内部（GhostlockApp 已是 MiuixTheme 的调用点）
MiuixTheme(colors = ...) {
    CompositionLocalProvider(LocalSpacing provides spacing) { ... }
}

// 消费方：只引用 token
Column(modifier = Modifier.padding(horizontal = LocalSpacing.current.pageHorizontal))
```
- **可复用的现成 token 优先于新 token**：Miuix 已提供一批（见 §4 表），例如 `CardDefaults.CornerRadius`、`CardDefaults.InsideMargin`、`BasicComponentDefaults.InsideMargin`、`SmallTitleDefaults.InsideMargin`、`DialogDefaults.insideMargin/outsideMargin/MaxWidth/CornerRadius`、`DropdownDefaults.InsideHorizontalPadding/FirstLastVerticalPadding/MiddleVerticalPadding`、`DividerDefaults.Thickness`。
- 组间距优先用 `Arrangement.spacedBy(token)` / `PaddingValues(token)`，**不要**用 `Spacer(Modifier.height(x.dp))`。
- **不引入 Material 3 的 `MaterialTheme` 来当 token 载体**：本项目依赖里没有 `androidx.compose.material3`（只有 `foundation` 与 `material-icons-extended`），且 M3 的 `MaterialTheme` 也**不含 spacing**（只有 color/typography/shape）。

**R2.4（强制）** 新增/例外必须**可被证伪**：任何豁免都要有「被用到否则 FAIL」的检查（照抄本项目 `CombinationTokenHardcodeTest` 的做法：stale 豁免即 FAIL）。见 R4。

### R3 按下态（indication）/ 阴影必须被组容器的形状裁剪

**R3.1（强制）** 按下高亮必须绘制在**被裁剪的子树内部**，否则高亮（直角矩形）会溢出组容器的圆角。
Miuix 依据：`MiuixIndication` 的实现是 `drawRect(color, alpha, size = size)`，**是直角矩形、自身不带形状**（Miuix 源码 `utils/MiuixIndication.kt` L129-L135）⇒ 必须由外层 shaped 容器裁剪。

**R3.2（强制）** 点击修饰符要加在**容器内部**（Miuix 组件自己就是这么做的）：`Card` 的可点击 `clickable` 位于带 `squircleSurface` 的 `Box` 之内（Miuix 源码 `basic/Card.kt` L136-L151 + L172-L181）⇒ 按下高亮自动被组形状裁剪。

**R3.3（建议）** 阴影（shadow）要按 shape 裁剪：Compose 的 `Modifier.shadow` 以 shape 决定阴影轮廓，`Modifier.clip` 的顺序会影响内容是否被裁；Miuix `Surface` 的私有 `surface` 修饰符给出的顺序是 graphicsLayer(shadowElevation, shape, clip=false) → `border` → `clip(shape)` → `background`（Miuix 源码 `basic/Surface.kt` L127-L146）。自定义容器时照此顺序。
来源：[Modifier.clip 与各类 draw 修饰符](https://developer.android.com/reference/kotlin/androidx/compose/ui/draw/package-summary)。

### R4 守卫：把「禁止手调间距」做成可失败的检查

**R4.1（强制）** 新增一个**源码扫描守卫测试**（建议 `app/src/test/kotlin/com/ghostlock/app/ui/UiDimensionHardcodeTest.kt`），仿 `CombinationTokenHardcodeTest.kt`（本仓已有同型守卫，「豁免必须被使用，stale 即 FAIL」）：
- 扫描 `app/src/main/kotlin/**/ui/**`（token 文件与必要的绘制例外进白名单）；
- 正则匹配 `[0-9]+.dp` / `[0-9]+.sp`；
- 命中即 FAIL 并打印 `file:line: literal`；
- 白名单条目**必须**被命中，否则 FAIL（防止豁免腐烂）。

**R4.2（强制，项目既有纪律）** 该守卫必须做一次**证伪实验**证明「能失败」：临时改回 `padding(top = 12.dp)` → 观察它以预期方式失败 → 撤回 → 确认无残留（本仓 AGENTS.md「守卫/断言必须证明『能失败』」）。

---

## 3. 权威来源链接

### 3.1 分组列表项形状（官方一手）

| 来源 | URL | 关键内容 |
| --- | --- | --- |
| androidx-main `ListItemDefaults.kt` | [android.googlesource.com](https://android.googlesource.com/platform/frameworks/support/+/refs/heads/androidx-main/compose/material3/material3/src/commonMain/kotlin/androidx/compose/material3/ListItemDefaults.kt) | `shapes()`（L404-L435）、**`segmentedShapes(index,count,defaultShapes)`（L439-L505）**、`SegmentedGap`（L~540）、`ListItemShapes` 类（L1064） |
| androidx-main `ListItem.kt` | [android.googlesource.com](https://android.googlesource.com/platform/frameworks/support/+/refs/heads/androidx-main/compose/material3/material3/src/commonMain/kotlin/androidx/compose/material3/ListItem.kt) | `SegmentedListItem(shapes = ListItemShapes, ...)` 的多个重载（`shapes` 为必填参数） |
| `ListItemDefaults` API 参考 | [developer.android.com](https://developer.android.com/reference/kotlin/androidx/compose/material3/ListItemDefaults) | 公开 API 索引 |
| `RoundedCornerShape` | [developer.android.com](https://developer.android.com/reference/kotlin/androidx/compose/foundation/shape/RoundedCornerShape) | per-corner 构造与 `topStart, topEnd, bottomEnd, bottomStart` 顺序 |
| M3 Lists / Menus / Shape | [Lists](https://m3.material.io/components/lists/overview) · [Menus](https://m3.material.io/components/menus/overview) · [Corner radius scale](https://m3.material.io/styles/shape/corner-radius-scale) | 分组视觉规范（组是一个连续色块，组内无分隔间隙） |

> ⚠️ **未证实**：m3.material.io 与 developer.android.com 的**正文由 JS 渲染**，本次 `web_fetch` 只能取到导航壳，**页面里的具体数值/措辞未经逐字核对**。上面的**代码级结论全部来自 androidx 源码原文**（已下载并逐行核对），不依赖网页正文。

### 3.2 间距 / 设计 token / CompositionLocal（官方一手）

| 来源 | URL | 关键内容 |
| --- | --- | --- |
| M3 Spacing tokens | [m3.material.io/styles/spacing/tokens](https://m3.material.io/styles/spacing/tokens) | Material 3 **有** spacing token 体系（⇒ 间距属于设计系统，不属于调用点） |
| M3 Design tokens | [m3.material.io/foundations/design-tokens/overview](https://m3.material.io/foundations/design-tokens/overview) | token 的定义与分层 |
| Compose：自定义设计系统 | [developer.android.com/…/designsystems/custom](https://developer.android.com/develop/ui/compose/designsystems/custom) | 官方如何自建设计系统（自定义 token 的落点） |
| Compose：主题剖析 | [developer.android.com/…/designsystems/anatomy](https://developer.android.com/develop/ui/compose/designsystems/anatomy) | 一个主题通常包含 color / typography / **shape**（M3 无 spacing 槽位） |
| Compose：CompositionLocal | [developer.android.com/…/compositionlocal](https://developer.android.com/develop/ui/compose/compositionlocal) | `staticCompositionLocalOf` vs `compositionLocalOf` 的选择、自定义 token 的传递机制 |
| Compose 测试总览 | [developer.android.com/…/testing](https://developer.android.com/develop/ui/compose/testing) | Compose 测试依赖与规则 |

### 3.3 Miuix（KernelSU 使用的 Compose 设计系统）

| 来源 | URL | 关键内容 |
| --- | --- | --- |
| 仓库 | [github.com/compose-miuix-ui/miuix](https://github.com/compose-miuix-ui/miuix) | Apache-2.0，Compose Multiplatform；本机缓存版本 **0.9.4**（与 KernelSU 一致） |
| 官方文档 | [compose-miuix-ui.github.io/miuix](https://compose-miuix-ui.github.io/miuix/) · [中文](https://compose-miuix-ui.github.io/miuix/zh_CN/) | 组件清单与属性表 |
| `Card.kt` | [blob/main/miuix-ui/.../basic/Card.kt](https://github.com/compose-miuix-ui/miuix/blob/main/miuix-ui/src/commonMain/kotlin/top/yukonga/miuix/kmp/basic/Card.kt) | `CardDefaults.CornerRadius = 16.dp`、`InsideMargin = PaddingValues(0.dp)`、`squircleSurface` 裁剪整个内容 |
| `SquircleBackground.kt` | [blob/main/miuix-squircle/.../SquircleBackground.kt](https://github.com/compose-miuix-ui/miuix/blob/main/miuix-squircle/src/commonMain/kotlin/top/yukonga/miuix/kmp/squircle/SquircleBackground.kt) | **per-corner** `squircleSurface/squircleBackground/squircleClip`；API 33 以下回退 `RoundedCornerShape` |
| `MiuixTheme.kt` | [blob/main/miuix-ui/.../theme/MiuixTheme.kt](https://github.com/compose-miuix-ui/miuix/blob/main/miuix-ui/src/commonMain/kotlin/top/yukonga/miuix/kmp/theme/MiuixTheme.kt) | 主题只有 `colorScheme`/`textStyles`（**无 shape/spacing token**） |
| 主题文档 | [docs/guide/theme.md](https://github.com/compose-miuix-ui/miuix/blob/main/docs/guide/theme.md) | 原文：The theme system consists of color schemes and text styles. |
| `Component.kt` | [blob/main/miuix-ui/.../basic/Component.kt](https://github.com/compose-miuix-ui/miuix/blob/main/miuix-ui/src/commonMain/kotlin/top/yukonga/miuix/kmp/basic/Component.kt) | `BasicComponentDefaults.InsideMargin = PaddingValues(16.dp)`、`heightIn(min = 56.dp)` |
| `Dropdown.kt` | [blob/main/miuix-ui/.../basic/Dropdown.kt](https://github.com/compose-miuix-ui/miuix/blob/main/miuix-ui/src/commonMain/kotlin/top/yukonga/miuix/kmp/basic/Dropdown.kt) | **Miuix 自己的分组做法**：`DropdownDefaults` 的 `FirstLastVerticalPadding`/`MiddleVerticalPadding`/`InsideHorizontalPadding`；`DropdownEntry` = 一个视觉组 |
| `DropdownEntriesContent.kt` | [blob/main/miuix-preference/.../popup/DropdownEntriesContent.kt](https://github.com/compose-miuix-ui/miuix/blob/main/miuix-preference/src/commonMain/kotlin/top/yukonga/miuix/kmp/popup/DropdownEntriesContent.kt) | 组内首/末只用**不同 padding**、组间用 `HorizontalDivider`；**组内行没有各自形状** |
| KernelSU 实例 | [HomeMiuix.kt#L443-L470](https://github.com/tiann/KernelSU/blob/main/manager/app/src/main/java/me/weishu/kernelsu/ui/screen/home/HomeMiuix.kt#L443-L470) | 「一个 `Card` 包多个 preference 行」是 KernelSU 的既有骨架 |
| KernelSU 依赖 | [manager/gradle/libs.versions.toml](https://github.com/tiann/KernelSU/blob/main/manager/gradle/libs.versions.toml) | `miuix = "0.9.4"` |

### 3.4 Robolectric 像素断言

| 来源 | URL | 关键内容 |
| --- | --- | --- |
| androidx ui-test `WindowCapture.android.kt`（androidx-main） | [cs.android.com](https://cs.android.com/androidx/platform/frameworks/support/+/androidx-main:compose/ui/ui-test/src/androidMain/kotlin/androidx/compose/ui/test/android/WindowCapture.android.kt) | `forceRedraw` 的 Robolectric 分支：**跳过强制重绘**，注释明说否则测试时钟会挂起超时；随后走同步 `PixelCopy` shadow |
| robolectric#8071 | [github.com/robolectric/robolectric/issues/8071](https://github.com/robolectric/robolectric/issues/8071) | 历史病因与结论（2023，**至今 open**）：旧版 `captureToImage` 在 Robolectric 下超时；官方结论是「它强制重绘，Robolectric 做不到」 |
| Robolectric 4.10 发布说明 | [releases/tag/robolectric-4.10](https://github.com/robolectric/robolectric/releases/tag/robolectric-4.10) | 引入 Robolectric Native Graphics（RNG） |
| `GraphicsMode` Javadoc | [robolectric.org/javadoc/latest/…/GraphicsMode.html](https://robolectric.org/javadoc/latest/org/robolectric/annotation/GraphicsMode.html) | `LEGACY` / `NATIVE` 两种图形 shadow；注解目标 TYPE/METHOD/PACKAGE |
| Roborazzi | [github.com/takahirom/roborazzi](https://github.com/takahirom/roborazzi) | JVM 截图测试的事实标准；README 明确「支持 RNG，需 `@GraphicsMode(GraphicsMode.Mode.NATIVE)`」；若自研像素断言成本过高可作为替代方案 |
| Compose 测试 | [developer.android.com/…/testing](https://developer.android.com/develop/ui/compose/testing) | Compose 测试依赖（含 `ui-test-manifest`） |

---

## 4. 本项目落地映射（Miuix 0.9.4 组件与 token 对照）

### 4.1 组件映射

| 需求 | 本项目应使用的 Miuix 组件 | 说明 |
| --- | --- | --- |
| 一个分组（设置页/菜单的一坨） | `Card` | **一个组 = 一个 `Card`**；content 是 `ColumnScope`，直接罗列行 |
| 可点击的设置行（带标题+副标题） | `BasicComponent` / `ArrowPreference` | 行自身**无背景**，由外层 `Card` 提供 |
| 单选行（本项目「执行组合」用） | `RadioButtonPreference` | 同上；`radioButtonLocation = RadioButtonLocation.End` |
| 开关行 | `SwitchPreference` | 同上 |
| 下拉/选择器 | `OverlaySpinnerPreference` / `OverlayDropdownMenu` | 后者的 `entries: List<DropdownEntry>` **原生支持多组**，见 §4.3 |
| 组标题 | `SmallTitle` | token：`SmallTitleDefaults.InsideMargin = PaddingValues(28.dp, 8.dp)` |
| 组内分隔 | `HorizontalDivider` | token：`DividerDefaults.Thickness = 0.75.dp` |
| 弹窗 | `OverlayDialog` | token：`DialogDefaults.insideMargin = DpSize(24.dp, 24.dp)`、`outsideMargin = DpSize(12.dp, 12.dp)`、`MaxWidth = 420.dp`、`CornerRadius = 32.dp` |
| 底部抽屉 | `OverlayBottomSheet` | `BottomSheetDefaults` |
| 自定义形状/裁剪 | `squircleSurface` / `squircleClip` / `squircleBackground` | per-corner 版本可表达 R1.3 的四角规则 |

### 4.2 token 映射

| 用途 | Miuix token（现成，直接用） | 值 |
| --- | --- | --- |
| 分组容器圆角 | `CardDefaults.CornerRadius` | `16.dp` |
| 分组容器内边距 | `CardDefaults.InsideMargin` | `PaddingValues(0.dp)` |
| 设置行内边距 | `BasicComponentDefaults.InsideMargin` | `PaddingValues(16.dp)` |
| 行最小高度 | （`BasicComponent` 内部） | `56.dp` |
| 组标题内边距 | `SmallTitleDefaults.InsideMargin` | `PaddingValues(28.dp, 8.dp)` |
| 弹窗内外边距 / 圆角 / 最大宽 | `DialogDefaults.insideMargin / outsideMargin / CornerRadius / MaxWidth` | 24,24 / 12,12 / `32.dp` / `420.dp` |
| 下拉组内首末/中间纵向内边距 | `DropdownDefaults.FirstLastVerticalPadding / MiddleVerticalPadding` | `20.dp` / `12.dp` |
| 下拉行横向内边距 | `DropdownDefaults.InsideHorizontalPadding` | `20.dp` |
| 分隔线粗细 | `DividerDefaults.Thickness` | `0.75.dp` |
| 字色 / 文字样式 | `MiuixTheme.colorScheme.*` / `MiuixTheme.textStyles.*` | CompositionLocal 提供 |
| **本项目自有间距（缺失，需新建）** | 建议 `GhostlockSpacing` + `LocalSpacing`（`staticCompositionLocalOf`） | 见 R2.3 |

### 4.3 「执行组合」菜单的推荐骨架（建议，未实施）

Miuix 的 `OverlayDropdownMenu`（`entries: List<DropdownEntry>`，文档措辞 one or more groups）本身就是「多组菜单」的现成组件：组内行用位置相关的 **padding**，组之间用 `HorizontalDivider`，**行没有自己的形状**。若本项目的「执行组合」继续用弹窗 + 单选行，最小骨架是：

```kotlin
// 每个 (backend, route) 组 = 一个 item + 一个 Card（不是每行一个 Card）
item(key = "group:" + group.backend + "|" + (group.route ?: "-")) {
    Card {                                   // 组容器：唯一的形状来源
        for (leaf in group.leaves) {         // 行：无自己的容器、无间隙
            RadioButtonPreference(
                title = ...,
                summary = ...,
                selected = leaf.entry == state.executionComboDraft,
                onClick = { actions.onExecutionComboDraftChanged(leaf.entry) },
                enabled = leaf.selectable,
                radioButtonLocation = RadioButtonLocation.End,
            )
        }
    }
}
```
要点：① 组内没有任何 `Spacer`；② 组间距来自分组之间（用 token），不是行之间；③ 圆角只出现在组容器上（首行上两角、末行下两角由容器裁剪自动得到）。

---

## 5. 本项目当前违规点清单

> 基线：`GhostlockUI.kt` @ `4e0ff25a…`、`AdvancedUI.kt` @ `d34b42d4…`。**文件正被其他 writer 修改**，行号可能已漂移，请以符号名定位。

### 5.1 形状类（直接对应用户报告 1）

| # | 位置 | 违规 | 后果 | 最小改法 |
| --- | --- | --- | --- | --- |
| V1 | `GhostlockUI.kt` `ExecutionCombinationDialog`（L568 起；`LazyColumn` L586；`itemsIndexed` L596；**`Card {` L607 只包一个 `RadioButtonPreference` L608**） | **一行一个 `Card`** ⇒ 每一行都是独立的 16dp 四角圆角容器 | 组的末项「上下全圆角」（用户报告的现象）；相邻行两个圆角相接 ⇒ 相接处出现**异常间隙** | 把 `itemsIndexed(group.leaves)` 换成**每组一个 `item` + 一个 `Card` 包 `group.leaves.forEach`**（见 §4.3） |
| V2 | `AdvancedUI.kt` `BuiltinProfileRow`（L924-L939，`Card(modifier.fillMaxWidth())` L931） | 一行一个 `Card` + 外层用 `Modifier.preferencePageItem()` 逐行分隔（L913 等调用点） | 每个内置 profile 都是独立圆角块；连续多行时同样出现「圆角缺口」式间隙 | 若这些行在视觉上属于同一组：改为**组 `Card`**；若确实每组只有一行，则保持现状但**间距改走 token** |

### 5.2 间距类（直接对应用户报告 2）

统计（脚本实测，`[0-9]+ .dp` / `[0-9]+ .sp` 字面量，`app/src/main/kotlin/com/ghostlock/app/ui/`）：

| 文件 | `.dp` 字面量 | `.sp` 字面量 | `Spacer(` 次数 |
| --- | --- | --- | --- |
| `AdvancedUI.kt` | 42 | 6 | 9 |
| `GhostlockUI.kt` | 36 | 6 | 5 |
| `PayloadSettingsUI.kt` | 29 | 0 | 1 |
| `PluginSettingsUI.kt` | 19 | 0 | 1 |
| `PluginDetailUI.kt` | 17 | 0 | 0 |
| `AboutUI.kt` | 15 | 3 | 1 |
| **合计** | **158** | **15** | **17** |

典型违规点（建议改法同栏）：

| # | 位置 | 违规 | 最小改法 |
| --- | --- | --- | --- |
| V3 | `GhostlockUI.kt` `pageContentPadding`（L504） | `top = 8.dp, bottom = 12.dp, horizontalMin = 12.dp`、`800.dp` 全是字面量，且把「最大内容宽」写死在 UI | 三个默认值 + `800.dp` 提为 token（`GhostlockSpacing.pageHorizontal / maxContentWidth`） |
| V4 | `GhostlockUI.kt` L908 / L937 `Card(modifier = modifier.padding(top = 12.dp))` | 分组间距手写 | 换成 `GhostlockSpacing.sectionGap`（或让相邻 `Card` 由统一容器用 `Arrangement.spacedBy` 排列） |
| V5 | `GhostlockUI.kt` 弹窗内 `.padding(top = 16.dp)`（L694/L724/L753）、`Spacer(Modifier.width(12.dp))`（L701/L731/L762/L796） | 弹窗按钮排布手写间距 | 用 `Row(horizontalArrangement = Arrangement.spacedBy(token))` 去掉 `Spacer` |
| V6 | `GhostlockUI.kt` L665 `.padding(bottom = 12.dp)`（LIST 弹窗逐项） | 逐项手写底距 | 由 `LazyColumn(verticalArrangement = Arrangement.spacedBy(token))` 承担 |
| V7 | `GhostlockUI.kt` `ActivationStatusCard` L1061-L1062 `offset(x = 27.dp, y = 31.dp)` / `size(110.dp)` | 装饰图标用魔数定位 | 该图标是**纯装饰**：建议改为布局内（`Box` + `Alignment`）表达，或把尺寸/偏移收进 token；否则这是唯一无法用「自动排版」表达的例外，必须显式豁免并注明理由 |
| V8 | `GhostlockUI.kt` L1072 附近 `Spacer(Modifier.height(1.dp))` | 用 1dp Spacer 当行距 | 用 `Text` 自带行距 / `Arrangement.spacedBy(token)` |
| V9 | `AdvancedUI.kt` L917 `Spacer(Modifier.height(24.dp).navigationBarsPadding())` | 底部留白手写 | 用 `Scaffold` 的 `contentPadding` / `WindowInsets` + token |
| V10 | 全 UI：`fontSize = 22.sp / 15.sp / 14.sp`（如 `GhostlockUI.kt` `ActivationStatusCard`、`CombinationSelector`） | 字号手写，绕过 `MiuixTheme.textStyles` | 改用 `MiuixTheme.textStyles.*`（Miuix 已提供 CompositionLocal 化的字体 token） |

---

## 6. 反例清单（不要这么写）

| # | 反例 | 为什么错 | 正确写法 |
| --- | --- | --- | --- |
| N1 | `for (leaf in leaves) { Card { Row(leaf) } }`（本项目 V1） | 一行一容器 ⇒ 末项四角全圆 + 相邻圆角间出现缺口式间隙 | `Card { for (leaf in leaves) Row(leaf) }` |
| N2 | `Card { A() }` + `Spacer(Modifier.height(12.dp))` + `Card { B() }` 当作「一组两行」 | 组内被 `Spacer` 撑开 ⇒ 视觉上不是一组；且 `12.dp` 是手写间距 | 同一 `Card` 内罗列 A、B，无 `Spacer` |
| N3 | 给**首行/末行**分别写不同的 `RoundedCornerShape` 并各自 `clip` | 圆角值散落调用点；与组容器不一致；改一次要改 N 处 | 用组容器统一裁剪；确需 per-position 时用 `ListItemDefaults.segmentedShapes`（M3）或 `squircleSurface(topStart=…, bottomEnd=…)`（Miuix）+ `CardDefaults.CornerRadius` |
| N4 | `Modifier.clip(shape)` 加在**点击修饰符之外**（如 `Modifier.clickable{}.clip(shape)`） | 按下高亮绘制在裁剪之外 ⇒ 直角高亮溢出圆角 | 裁剪在点击之内：`Modifier.clip(shape).clickable{}`，或把点击放进被裁剪的子树（Miuix `Card` 的做法） |
| N5 | `padding(horizontal = 24.dp)` / `Spacer(Modifier.height(12.dp))` / `size(110.dp)` / `fontSize = 22.sp` | 手写基本数据；无法随主题/密度/无障碍缩放统一调整；同一意图在多处出现不同数值 | 组件默认值 → Miuix `XxxDefaults` → 本项目 `LocalSpacing` token；字体用 `MiuixTheme.textStyles` |
| N6 | `Spacer(Modifier.width(12.dp))` 放在 `Row` 中间做按钮间距 | 用「占位符」表达布局意图，删除/新增元素时要成对维护 | `Row(horizontalArrangement = Arrangement.spacedBy(token))` |
| N7 | 「因为 KernelSU 是这么写的，所以我们也写 dp」 | **前提不成立**：KernelSU 的 Miuix 界面自己就手写 dp（实测 `HomeMiuix.kt` 30 处、`ModuleMiuix.kt` 69 处、`SuperUserMiuix.kt` 36 处） | 移植**骨架与样式**（Card 包行、用 `XxxDefaults`、用 `MiuixTheme`），但**不移植它的手写 dp**；间距由本项目 token 层承担 |
| N8 | 把 spacing token 放进 `MaterialTheme` 扩展 | 本项目依赖里**没有** `androidx.compose.material3`（只有 `foundation`、`material-icons-extended`），且 M3 的 `MaterialTheme` 也不含 spacing | 自带 `GhostlockSpacing` + `staticCompositionLocalOf`，挂在 `MiuixTheme` 内侧 |

---

## 7. 回归测试：Robolectric 下用像素断言四角（可行性 + 模板）

### 7.1 可行性结论：**有条件可行**

| 前提 | 结论 | 证据 |
| --- | --- | --- |
| `captureToImage()` 在 Robolectric 下是否可用 | **可用**（前提见下）。compose ui-test **1.12.1**（本项目版本）源码含 Robolectric 分支：`HasRobolectricFingerprint`（= `Build.FINGERPRINT.lowercase() == "robolectric"`）时 `View.forceRedraw` **直接 return**，注释原文：We skip this on Robolectric … Callbacks like FrameCommitCallback will never trigger, causing the test clock to hang and time out. Furthermore, Robolectric's PixelCopy shadow executes synchronously | 本机解包 `ui-test-android-1.12.1-sources.jar` → `androidMain/…/test/android/WindowCapture.android.kt` L133-L146；[cs.android.com 同文件（androidx-main）](https://cs.android.com/androidx/platform/frameworks/support/+/androidx-main:compose/ui/ui-test/src/androidMain/kotlin/androidx/compose/ui/test/android/WindowCapture.android.kt) |
| 图形模式 | 需 `@GraphicsMode(GraphicsMode.Mode.NATIVE)`（legacy 图形的 `Bitmap` 是假实现，取不到真像素） | [GraphicsMode Javadoc](https://robolectric.org/javadoc/latest/org/robolectric/annotation/GraphicsMode.html)；[robolectric 4.10 说明](https://github.com/robolectric/robolectric/releases/tag/robolectric-4.10) |
| SDK 下限 | **≥ 26**：捕获路径 `Window.generateBitmap` 带 `@RequiresApi(Build.VERSION_CODES.O)` 并走 `PixelCopy` | 同 1.12.1 源码 L172-L195 |
| Android 资源 | 需要 `testOptions.unitTests.isIncludeAndroidResources = true` —— **本项目已开**（`app/build.gradle.kts` L105-L107） | [Compose 测试](https://developer.android.com/develop/ui/compose/testing) |
| 版本 | 本项目 `robolectric:4.17`、`ui-test-junit4:1.12.1`（已有） | `app/build.gradle.kts` L539/L542 |
| ⚠️ 弹窗 / popup / SurfaceView 内的节点 | **不可用**：这些节点走 `processMultiWindowScreenshot`，底层是 `UiAutomation`（instrumentation 能力），Robolectric 无此能力 | 1.12.1 `AndroidImageHelpers.android.kt` L64-L77 |
| ⚠️ 形状回退 | **API < 33** 时 Miuix 的 squircle 回退为 `RoundedCornerShape`（圆弧角，不是连续角）⇒ 断言要写成「直角 vs 圆角」这一层级，不要断言具体轮廓像素 | Miuix `SquircleBackground.kt` L159-L164 + 官方 squircle 文档 |
| ⚠️ 抗锯齿 | 角点像素是**半透明混合**（squircle 用 `smoothstep` 做边缘）⇒ 必须容差比较或离角点 1-2px 采样 | Miuix `SQUIRCLE_SHADER` 的 `smoothstep(0.5, -0.5, dist)` |

> 历史背景（**已不适用**但请勿误引）：[robolectric#8071](https://github.com/robolectric/robolectric/issues/8071) 报告的「`captureToImage` 在 Robolectric 超时」是 2023 年旧版行为（issue 至今 open，但 androidx 已加 Robolectric 分支）。**用 issue 现状判断「不可用」是过时结论**；请以本项目实际版本的源码为准。

### 7.2 测试模板（可直接落地为 `app/src/test`，本次**未**创建）

```kotlin
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])                                    // >= 26；与本仓既有测试一致
@GraphicsMode(GraphicsMode.Mode.NATIVE)                // 关键：真像素
class GroupedRowShapeTest {

    @get:Rule val composeTestRule = createComposeRule()

    /** 组 = 一个 Card；末行的「下两角圆、上两角直」来自组容器裁剪。 */
    private fun setGrouped() = composeTestRule.setContent {
        MiuixTheme(colors = lightColorScheme()) {
            Box(modifier = Modifier.testTag("host").background(Color.White)) {
                Card(modifier = Modifier.testTag("group")) {
                    BasicComponent(title = "first", onClick = {})
                    BasicComponent(title = "last", onClick = {})
                }
            }
        }
    }

    private fun assertCorners(tag: String) {
        val px = composeTestRule.onNodeWithTag(tag).captureToImage().toPixelMap()
        val w = px.width
        val h = px.height
        // 离角点 2px 采样，避开抗锯齿；容差比较
        fun near(a: Color, b: Color, tol = 24) =
            kotlin.math.abs(a.red - b.red) * 255 < tol &&
                kotlin.math.abs(a.green - b.green) * 255 < tol &&
                kotlin.math.abs(a.blue - b.blue) * 255 < tol
        val groupBg = px[w / 2, 2]          // 组容器自身填充色（上边中点，必定被填充）
        val hostBg = px[0, 0]               // 宿主背景（组容器角点外侧）
        // 上两角为直角 => 角点被组容器填充（= groupBg，不是宿主背景）
        assertTrue("top-left must be square (filled)", near(px[0, 0], groupBg))
        assertTrue("top-right must be square (filled)", near(px[w - 1, 0], groupBg))
        // 下两角为圆角 => 角点透出宿主背景
        assertTrue("bottom-left must be rounded", near(px[0, h - 1], hostBg))
        assertTrue("bottom-right must be rounded", near(px[w - 1, h - 1], hostBg))
    }

    @Test
    fun group_lastRow_hasSquareTop_andRoundedBottom() {
        setGrouped()
        assertCorners("group")              // 组容器：四角圆；末行的下两角即组的下两角
    }
}
```

**断言「组内无异常间隙」**（可选，更直接地对应用户报告）：
- 结构法（推荐，稳）：取相邻两行的 `getUnclippedBoundsInRoot()`，断言 `row2.top == row1.bottom`（组内相邻行必须严格相接）。
- 像素法：对**整组** `captureToImage()`，沿垂直中线扫描，断言两行交界处**不存在**宿主背景色像素带（圆角缺口会在两侧出现宿主色，中线扫描抓不到 ⇒ 需同时扫 x≈2 与 x≈w-3 的竖线）。

**证伪实验（强制）**：把 `Card { A(); B() }` 改回「一行一个 `Card`」，`bottom-left must be rounded` 仍会通过（每行都圆），但**组内相邻行边界**会从「严格相接」变成「两角缺口」⇒ 结构法断言必须失败；同时 `top-left must be square` 在**行**节点上会失败。**报告里要写明造错点与失败输出**（本仓 AGENTS.md 纪律）。

**依赖缺口（未证实）**：本项目 `testImplementation` 只有 `ui-test-junit4:1.12.1`，**没有** `androidx.compose.ui:ui-test-manifest`（`app/build.gradle.kts` L542 附近的注释写着「未确认必需，暂不加」）。Compose 官方测试文档把 `ui-test-manifest` 列为 `createComposeRule()` 的前置依赖（它提供 `ComponentActivity` 的 manifest 条目）。**本次未跑 Gradle，无法实测** ⇒ 标记为「未证实，需在写测试时先跑一次确认」。

---

## 8. 未证实 / 需裁决

1. **未证实**：m3.material.io 与 developer.android.com 的**正文**（JS 渲染）未被逐字读取；本文的代码级结论全部来自 androidx 与 Miuix **源码原文**，不受影响。
2. **未证实**：`ui-test-manifest` 是否为本项目 JVM Compose 测试的必需依赖（见 §7.2 末）。
3. **未证实**：`ListItemDefaults.segmentedShapes` / `SegmentedListItem` 属于 **androidx-main**（未发布线）；本项目**并不依赖 material3**，该 API 仅作为「官方范式」引用，**不建议**为它引入 material3 依赖。
4. **需用户裁决（与既有要求的冲突，必须先讲清）**：
   - 用户要求「移植 KernelSU 的控件骨架与样式」**并且**要求「UI 层禁止手调间距」。事实是 **KernelSU 自己的 Miuix 界面仍在手写 dp**（§6/N7 实测数据）⇒ 两条要求不能同时以「照抄 KernelSU」实现。**建议取舍**：移植骨架（`Card` 包行、`XxxDefaults`、「零参数」优先）＋ 由本项目自建 token 层承载间距；**不**移植 KernelSU 的手写 dp。
   - 「禁止手调间距」的**严格边界**需裁决：是否连 `Modifier.offset(x = 27.dp, y = 31.dp)` 这种**纯装饰性**（非排版意图）的定位也禁止？我建议：禁止，但允许在守卫测试里**具名豁免 + 注明理由**（照 `CombinationTokenHardcodeTest` 的豁免机制），避免把装饰性微调逼成「为了过检查而写 token」的形式主义。
   - 是否引入 Roborazzi（现成 JVM 截图测试）而不是自研像素断言：Roborazzi 更省事，但引入一个新依赖与基线图管理流程；自研模板（§7.2）零新依赖但需自己处理容差。**建议**：先按 §7.2 自研（一个测试文件即可），若后续要覆盖多屏/多主题再评估 Roborazzi。
