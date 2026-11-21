# 已完成语义图标目录基线

## Context and scope

完整迁入的有效设计/范围与验收正文；不是新增实施批。原批准/历史结果按原Beads记录，工具工件齐备不代表实现或门禁通过。

来源：`043fdf6a:.beads/issues.jsonl`；保留语义代号和原锚点。本文中的历史进度/门结果文字仅是其具名输入的沿革，当前状态从proposal列出的Beads读取。旧机制由正文已注明的后继差异取代；基线不重新授权实施。

## Decisions

<a id="record-gl-rot"></a>

## P-UI-ICON-001：已完成语义图标设计与验收

<!-- migrated-source: 043fdf6a Beads gl-rot description -->
2026-11-21迁入的共享设计记录，不是新的执行任务。closed仅表示该记录收录完毕，不表示业务实现、新批准或门禁通过。
已完成语义目录，不等于后续全量图标gl-72l完成。

<!-- migrated-source: design gl-rot -->
<!-- source: f882e670:docs/plan/backend-root-execution-unification-plan.md:L648-L655 -->
##### P-UI-ICON-001/决策01 Material/Miuix 统一语义图标目录（已批准并落地）

项目新增唯一 `com.ghostlock.app.ui.icon.Icons`。在目录实现内部，把 Compose upstream `Icons` import alias 为 `MaterialIcons`；每个项目语义项显式映射一个 Material icon 与一个 Miuix icon，并由唯一 `resolve(UiMode)` 选择当前皮肤。交集与并集都允许：某一皮肤缺少对应项时只对该侧回退到问号/帮助图标，不影响另一侧。

P-UI-ICON-001采用人工维护的语义目录，不使用反射：Material 与 Miuix 图标主要以生成的 Kotlin extension property 暴露，反射会丢失 extension receiver、受 shrink/版本实现细节影响，并把编译期缺失变成运行时错误。本批只收录项目实际使用的语义，不为每个 screen 建第二 resolver。

P-UI-ICON-001/批次01将迁移当前所有直接 `androidx.compose.material.icons.Icons` 与 `MiuixIcons` callsite；批后只有 `ui/icon/Icons.kt` 可以直接引用两个上游目录。



<!-- source: f882e670:docs/plan/backend-root-execution-unification-plan.md:L1756-L1783 -->
<a id="p-ui-icon-001-batch-01"></a>

##### P-UI-ICON-001/批次01 全项目 Material/Miuix 语义图标收口（开发者验收完成）

###### 目标与 scope

先建立全项目唯一图标目录，避免 BackendUi 又产生第三套 resolver。新增 `ui/icon/Icons.kt`，人工声明当前实际使用的语义项与 Material/Miuix pair；把现有 30 个直接 upstream icon callsite 全部迁到项目 `Icons`。没有对应图标的一侧回退问号图标，另一侧仍使用真实映射。

本批只改变图标取得位置，不改页面布局、文案、click action、导航、状态判断或 backend 行为。exact callsite 由同批 ledger 固定；发现额外 production callsite 时先补 ledger，不顺手改其它 UI。

###### 验收

- production source 中 `androidx.compose.material.icons.Icons` 与 `MiuixIcons` 的直接引用只允许出现在 `ui/icon/Icons.kt`；测试可引用 fake pair，但不复制 production catalog。
- 每个已迁移语义在 Material3/Miuix 两套 UI 都能解析；至少一项验证单侧 fallback；RTL automirrored 图标语义不退化。
- 本批不使用反射、生成全库镜像、字符串图标名或 backend-private resolver；catalog 未设置人为数量上限。P-UI-ICON-002的全量生成需求独立设计，不在本批提前实现。
- 现有页面 screenshot/语义测试只验证图标选择与原 action 不变，不借本批重做页面视觉。

- 原已完成步骤、批准与运行证据：见 Beads `gl-rot` notes 的迁移前记录；本设计不维护进度checkbox。

###### 当前实施与验证边界（2026-11-08）

- `Icons.kt` 已提供88个current语义/变体pair；原30个production文件中的175条upstream icon import已归零，目录外import与fully-qualified引用均由architecture guard拒绝。`IconsTest`覆盖Material/Miuix选择、单侧Miuix Help fallback及AutoMirrored语义。
- `python3 tools/kotlin_architecture_d0.py verify --repo . --fixtures app/src/test/resources/architecture --section all`：exit 0，`D0_VERIFY=PASS`、`PROJECT_ICON_AUTHORITY=0`；`testDebugUnitTest`为114 tests / 0 failures / 0 errors / 0 skipped，`compileDebugAndroidTestKotlin` exit 0；`:app:assembleDebug :app:assembleRelease` exit 0；`nativeTestHost`为31/31。
- `nativeBuildAndroid`在clean前后均先于本批Kotlin编译失败于既有`verifyJnaDistribution`：current/HEAD `LGPL2.1` SHA-256均为`c38856d04dc924d23f8d18b83238c3d56c44a5ada7d33fc4e185a9430078c8da`，而current/HEAD `INVENTORY.txt`登记`eea173a556abac0370461e57e12aab266894ea6be3874c2be05fd87871f75449`。本批未修改JNA文件、Gradle gate或inventory；该矛盾保留为独立构建债，不再阻断开发者已经接受的图标UI验收，也不被独立assemble结果改写成JNA gate PASS。
- 2026-11-08开发者已手工检查当前UI并确认没有显示错误，明确关闭IC-V1；该用户观察只证明图标显示验收，不生成未执行的自动截图命令或JNA distribution PASS。



<a id="acceptance-gl-rot"></a>

### 验收定义与原边界：gl-rot

<!-- migrated-source: acceptance_criteria gl-rot -->
保留原文各行为判据、当轮证据和未关闭设备门。此decision收录完成不关闭任何关联业务任务。

批准/审核/实际结果：Beads `gl-rot` notes；本段不维护实时勾选或新PASS。
