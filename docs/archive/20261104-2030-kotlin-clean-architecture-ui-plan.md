# 归档说明

- 原路径：`docs/plan/kotlin-clean-architecture-ui-plan.md`
- 归档日期：2026-11-04 22:24:59 -0500
- 归档原因：completed/current-fact plan superseded by root execution authority；用户于 2026-11-04 批准 Backend root execution D1–D9 与 R0→R1 收口，本文件不再作为活动实施 authority。
- 来源：`dev-nocpp` 当前工作树；正文原样保留。现行唯一后继为 [`../plan/backend-root-execution-unification-plan.md`](../plan/backend-root-execution-unification-plan.md)。

---

# Kotlin Clean Architecture 与语义 UI 收敛计划（2026-11-03）

> 状态：V0.12 与 D1-D6 source 收口已完成；未执行的历史 D0.3/A301SO 攻击门仍不得写成通过。D7 正按本文件 §9 修订“只通过标准化 BackendSpec 暴露 backend 能力”的剩余边界；实时状态以 `docs/analysis/kotlin-clean-architecture-d0.md` 为准。
> 2026-11-04 范围澄清：D7 没有D7.6或新的“后续边界”；其既有设备门未通过也不等于新整理范围。用户随后裁定 existing backend 不作兼容基线、backend 与 app/core 同改；错误引入 post-root executor 的 KBR/KC 稿已归档，新的独立 L 级需求入口是[`backend-root-execution-unification-plan.md`](backend-root-execution-unification-plan.md)。
> 级别：L。基线：branch `dev-nocpp`，HEAD ffa6497adfc12938133e6338e2f95ddbf2ed54b9，2026-11-03T07:17:38+00:00；`:app:compileDebugKotlin` 基线 exit 0。
> 目标：Kotlin 按 Clean Architecture 收敛；删除 KernelSU 管理器移植残留，但保留真实 KSU 攻击/交接代码与 `ui/component` 的可复用能力；UI 统一语义组件，保持现有 `LocalUiMode` 下 Material/Miuix 外观。
> 从属执行数据：[附件 A](kotlin-clean-architecture-ui-plan-annex-a.md)（依赖、批次、所有权、KSU 边界）与[附件 B](kotlin-clean-architecture-ui-plan-annex-b.md)（验证、视觉、真机、失败恢复）。架构/决策只以本文为权威，附件不得另改决策。

## 0. 活跃计划与先后 gate

| 对象 | 裁定 |
|---|---|
| `flow-process-launcher-plan.md` | 继续有效，Flow/AIDL/new-process/handoff/keepalive/cancel 均冻结。D1 前须先完成原 artifact 的 B4-real，或由用户明确裁定“暂停 B4-real”并记录 APK/ELF/profile hash、未偿债与恢复条件；不得默认为暂停 |
| `backend-registry-unification.md` | 本计划是局部 amendment：保留一 backend object 与一个 `Backends` membership authority；旧 `BackendSpec.ConfigCard` ownership 已被 D5e/D7 §9 取代。旧计划只保留历史，D7 同批把其头部指向本文 §9 |
| `backend-directory-cleanup.md` | M1-M5 为当前目录权威；D0 补其未完成的最终 UI 走查，不恢复 active/unfinished 双树 |
| `43499-kotlin-package-refactor-plan.md` | 设计曾批准，但目标 `backend.cve*` 已被 M5 的 `backend/backends/*` 推翻；立即视为冻结，实施前必须重基线/重审；本计划不执行其 package/JNI phase，批准同批写暂停指针 |
| `ksu-port-remnants.md` / `ksu-ui-component-inventory.md` | 历史事实保留；“UI 整体保留/删除部分 component”的旧结论被本次目标取代 |
| `jni-two-backend-plan.md` UI 段 | 被本文取代；不得恢复第二套 selector/theme |

阶段门：
1. **G0**：本计划 design-critic 阻断 0 + 用户仅批准 D0。
2. **G1**：D0 产出五份 machine manifest（附件 A §1）并完成 M5 UI 走查；用户对 B4-real 选择“先完成”或“明确暂停”；design-critic 与用户再次批准 exact file/surface/KSU ledger。G1 前不得写 `app/src/main`。
3. **G2**：D1-D5 每个单变量子批全绿才前进；D5 改 dispatcher/use-case 边界后，两个 selectable production backend 都必须过真实冷机门。否则只能记“实现待门禁”，不得进 D6/宣称完成。

## 1. 现状事实与范围

- 启动链为 `GhostLockApp -> MainActivity -> HorizontalPager(主页/后端/执行/设置)`；`NavDisplay` 承载二级页；页面按 `LocalUiMode` 分 Material/Miuix。结构、四页顺序与可见外观保留。
- `ui` 有 206 个 Kotlin 文件，`ui/component` 有 71 个文件；只读 scanner 发现 98 个 public/internal Composable 声明行、至少 51 个 public/internal 顶层非 Composable API，且 16 个文件没有 public/internal Composable。故 component 保留门不能只数 Composable。`GhostLockApp` 写全局 `ksuApp`；Home/Settings/MainActivity 读固定值 `Natives/KsuCli`；KSU profile/module/template/sulog/ZIP/deep-link 路由仍在。
- `RunPage.kt` 731 行且复制两套运行状态/dispatch，Compose 直接碰 Context、文件、Intent、进程。
- `RunPage.kt` 的 `RunState/TerminalEntry` 为 private，Material/Miuix renderer 各自 `remember` 私有状态且入口只收 `bottomInnerPadding`；D0 的 androidTest 无法向**生产 renderer**注入 Running/Blocked/Handoff/Done/long-log。复制一份 test-only renderer 不构成生产视觉基线，真实 dispatch 又不具确定性且可能触及攻击路径。
- 当前 `core` **不是纯层**：含 Android/Compose/data/backend/JNI 依赖。因此本计划不把 `core` 整体称为 domain，也不允许新 domain 依赖它。
- 范围外：native/JNI/wire/profile、Flow host 生命周期与真实攻击算法；只在 adapter 端把 domain DTO 映射到现有合同。

## 2. 目标依赖结构

~~~mermaid
flowchart LR
  ROOT[GhostLockApp composition root] --> UI[ui presentation]
  ROOT --> AD[data and execution adapters]
  ROOT --> REG[backend registry snapshot]
  UI --> DOM[domain pure models ports use cases]
  UI --> KIT[ui component and semantic kit]
  AD --> DOM
  AD --> LEG[existing core backend launcher shizuku jni log]
  AD --> SPEC[BackendSpec outer plugin contract]
  REG --> SPEC
  B[concrete backend objects] --> SPEC
  B --> DOM
~~~

- 运行调用：`UI -> domain use case -> port -> adapter -> existing execution`。
- 源码：`domain/**` 不得 import Android/Compose/`data/backend/core/jni/shizuku/log/ui/attack`；无 `domain -> core` 边。adapter 可以调用现有不纯合同，但只传 domain DTO 的显式映射。
- `ui/screen` 与 `ui/viewmodel` 最终不得 import `data/backend/core/jni/shizuku/log/attack`；`ui/component` 只依赖参数、UI token 与 neutral component model。
- `ui` 继续作为 presentation，避免 206 文件无收益迁名。Clean Architecture 判据为 import DAG 与 I/O 边界，不是把目录改叫 presentation。
- 机械判据见附件 A §5；禁止白名单。尚未清理的既有边只能留在未迁子批，子批结束即该范围零违规。

## 3. Backend registry 局部 amendment

保留名称 `BackendSpec` 作为 **Android outer plugin contract** 与每 backend 唯一 object；`Backends.all` 是全仓唯一注册集合。

- ~~每个 object 直接拥有 `ConfigCard()`，由 backend 指向 UI kit。~~ 该 D5 前口径已被 D5e 与 §9 D7 取代：backend object 只投影 neutral `BackendConfiguration`；UI 只按 neutral DTO 选择 semantic renderer，backend 不 import UI/Compose/kit。
- object 提供不可变即时 `BackendRuntimeSpec`（纯 domain DTO；flow symbol 仅为 String，adapter 才映射现有 core 类型）。它不得注册、缓存、持久化，也不得形成 list；`key` 必须与 owner object 相等。
- 每个进程的 composition root 只读一次 `Backends.all`，复制成带 version 的不可变 `BackendRegistrySnapshot`；app root=`GhostLockApp/AppContainer`，host roots=`FlowHostService`、`GhostlockUserService`。`BackendCatalogAdapter`、`BackendPresentationAdapter` 与 `FlowEntryRegistryAssembly` 都显式接收同一进程 snapshot，禁止各自再读 registry。跨进程比较 version/content hash，不共享对象；UI/domain 均不直接 import backend 实现。
- D7 进一步冻结：`BackendSpec` 不是 pure/neutral domain type；它可保留现有 Android `Context`/`@StringRes`/`R` 与 core `FlowSymbol` outer-contract 类型，只允许 composition/data/execution adapter 与 backend registry 持有。UI/domain/core 不持有或 import `BackendSpec`，只收 neutral DTO/port；concrete backend 的依赖箭头指向 `BackendSpec`，不得反画成 contract 依赖实现。
- 必测：唯一 key、selectable implies flowSymbol、object key=runtime key、snapshot 两投影集合相等；覆盖 register/unregister 前后 version，源码扫描不得出现第二 mutable registry/catalog/list。
- D0 不生效 amendment，也不改旧 registry 计划；G1 批准本 amendment 时才同批更新其指针。未批准不得改 `BackendSpec`。

## 4. UI 单一权威与外观不变量

- selector 只留 `UiMode + LocalUiMode`；prefs `ui_mode` 与默认 Miuix 不变。
- theme 只留一个生产 `GhostLockAppTheme`：当前 live `KernelSUTheme` 及 Material/Miuix 子函数按附件 A 的无冲突映射机械迁名；既存 dormant `GhostlockTheme` 在 caller 归零后删除，XML 既存 `Theme.GhostLock` 不改名。
- semantic surface 只留 `GhostlockKit + LocalUiKit`；root 根据 `LocalUiMode` 提供 Material/Miuix implementation。
- 每页一份 immutable `UiState`、一份 `Actions`、一个 control path。`*Material`/`*Miuix` 可作为薄 renderer 保留 page geometry，但不得读取 repository、文件或进程。
- component 双重保留：71 个 baseline 文件均有 file row，不得丢失/合并（可 1:1 path rename）；每个 public/internal reusable declaration（top-level 与 public/internal member 的 class/data class/interface/sealed/enum/object/typealias/property/function/extension、构造器及 Composable）均有 API row。private/local 可排除。身份使用附件 A 的无行号 canonical key 区分 overload；完整 visibility/参数名与类型/default/return/annotation/slot/资源/双皮肤/semantics 作为对拍属性。baseline API 只能 `preserve-exact`、`rename-1to1` 或 `inputize-signature-1to1`；target-only 新 API 必须是 G1 逐签的 `add-approved`。compile/reference fixture 覆盖全部，无 caller 也不例外。
- 视觉唯一矩阵、环境与像素阈值见附件 B §2。KSU 内容删除区域逐矩形登记；区域外不得以“内容变了”豁免。

## 5. 批次（每项独立门禁）

### V0 D0 前视觉纠偏（实时 checkbox）

进度唯一权威为 [V0 实时清单](kotlin-clean-architecture-ui-plan-v0-checklist.md)：`[ ]` 未开始/失败，`[~]` 唯一进行中，`[x]` 仅在时间戳与证据齐全后立即更新；状态变化同批追加 review log。

V0.8 的 110dp/BottomEnd 装饰图标位置与 V0.9 信息层级均保持。V0.10 已取消装饰图标的 120dp 布局占位；V0.11 又把 navigation 与完整 tag FlowRow 合并进同一个 action Row，消除了 badge 独占测量行。用户在 V0.11 final critic 期间进一步要求 Open/navigation 与 badge 靠卡片底部对齐，因此 V0.12 只把 Miuix 内容列组织为「header + bottom action」；共享 model、Material、caller、backend/readiness、字符串资源、tag 顺序/颜色、click、16dp 内容边界与装饰 icon geometry 均冻结。D0 仍待另行明确批准。

### D0 只读盘点/characterization

D0 只写附件 A §1 的 exact repo paths，`app/src/main` 全部只读：既有文件修改仅限 Gradle 两行 test dependency、旧 package plan 冻结指针、review log 追加；不授权 registry amendment、D1-D6、D5-pre或真实攻击。D0 冻结 file action、component 71-file/API 全覆盖、KSU REMOVE/KEEP、全部 88 个 UI surface 行与 dependency edge；只捕获当前可由 production renderer 确定性呈现的 68 个 screenshot/semantics，M5a 的 20 行标 `deferred-D5`，不得以 test-only 复制 renderer 冒充。输出须再经 G1 评审，不准备仓外 rollback/transaction artifact。

D0 AVD 唯一权威是 `Pixel_5/default_boot`：tool 以固定 port、`-read-only -no-snapshot-save -force-snapshot-load` 启动并独占 emulator，保存原 runtime identity 后按附件 B 逐字段 override/复读；成功、失败、中断均在 `finally` 恢复并复读，再停 emulator 并核对 AVD/snapshot hash 未变。禁止修改 `config.ini/default_boot`、复用外部 emulator、identity 失配后继续、或自动换 AVD/snapshot。

### D1 分层骨架

按 G1 exact manifest 拆为：D1a 纯 domain DTO/port/use case；D1b 单一 settings adapter；D1c 单一 device adapter；D1d AppContainer/ViewModel factory；D1e BuildConfig/KernelVersion consumer 迁移。一次只落一个 port/adapter；不得同批移动 repository、改 UI、删 stub。

### D2 component 去 KSU 耦合

按 component manifest 逐族：bottom bar 输入化；profile 组件改 neutral draft；markdown/context/bugreport/reboot capability 注入；日志文件名中性化；历史 KSU 命名 1:1 迁名。每个旧 API 以参数映射关联唯一 target，所有 caller 同子批迁移；每族独立 compile/reference fixture 与 semantics，71 个 component 文件零删除、能力零静默合并。

### D3 KSU 管理器残留删除

D3a 先移除 KSU route/deep-link/ZIP 可达性，并在 neutral action=0 后删除 `IntentDispatcher.kt`；D3b 再删 service/AIDL/repository/screen/viewmodel；D3c Home 改 GhostLock device/backend state；D3d Settings 删除假 SU/module controls；D3e 仅按资源 ledger 清理。每项单独 build；真实 KSU handoff KEEP 集合逐字不增不减。

### D4 语义 UI

逐页独立：Home、Backend、Settings、About、ColorPalette、Finish/FinishSettings，最后 Run shell。每页先共享 state/actions，再接 semantic components，再对拍两 renderer；上一页视觉/语义绿后才下一页。D4 的 Backend 页只改 shell，所有 BackendSpec/ConfigCard signature 与 object 由 D5e 唯一拥有。

### D5 执行边界

D5a domain `RunBackendUseCase/RunSessionPort`；D5b 单 adapter 映射现有 dispatcher；D5c Run ViewModel/state；D5d 两 renderer；D5e registry amendment。D5d 必须先形成 production `RunUiState + Actions -> Material/Miuix renderer` 纯渲染缝，再由 androidTest 向该**同一生产 renderer**注入 Running/Blocked/Handoff/Done/long-log，补齐 M5a 20 个基线并把总数从 68 升到 88；测试不得 dispatch。生命周期必须遵附件 A §3。D5 触及 exec，两个 production backend 的真实冷机门无条件执行；缺用户 opt-in 时停在“待门禁”。

### D6 收口

import DAG、KSU ledger、component manifest、visual matrix、unit/build/device 全绿；更新 `full-process-uml.md` 为 Kotlin 最终唯一权威图，本文图改成链接；更新历史清单/双语 README/评审日志。无临时旧码、双 selector、双 repository 或 build 产物入库。

## 6. CBN

| 决策 | 方案与当前工程量 | 每次扩展边际 | 结论/翻转条件 |
|---|---|---|---|
| 包名 | 保留 `ui`=不搬 206 文件；整体迁名至少 206 文件/约 33k 行 import/package diff，运行收益 0 | 新页均 1 package | 保留；只有独立 Gradle presentation module 获批才重审 |
| 装配 | 手写 container 预计 4 生产文件+3 test；Hilt/Koin 至少改 root/app Gradle、Application、各 VM/entry，预计 >=12 文件并增依赖 | 手写每 port 改 provider+fake=2 处；DI 每 binding 约1 annotation/module 处 | 手写；binding >12 或第二个共享 Android composition root 时转 DI |
| registry | 原位混合不闭合依赖；plugin+即时 projection 预计 BackendSpec+6 objects+2 adapters+3 tests=12 文件；双 registry 需2 list+一致性层 | projection 新 backend=1 object+1 registry entry；双 registry=2 entry+一致性 test，>=3 处 | 选 projection；只有 UI/runtime 可独立动态卸载且有设备用例时转双 registry |
| semantic UI | 现有 2 皮肤/4 主页；kit 预计改2 implementation+每页1 common control；node tree 另需 node/dispatcher/serializer >=6 文件 | kit 新 primitive=2 implementation；双业务 renderer 每修复=2处；node 新 type>=3处 | 选 kit；只有 UI 描述需跨进程持久化时转 node tree |
| Run owner | 现有 Compose/VM suspend job=0新文件但 UI death 会取消；Application coordinator 预计3生产文件+6 lifecycle tests；Service/WorkManager authority >=5生产文件且形成第二终态面 | coordinator 每 executor=1 adapter、每 backend=0；最多1 active observation/control record，0 observer 仍只留到 terminal/handoff；不持久化、不复活进程 | 选 coordinator，但 terminal/result 仍以现有 host/launcher 为权威；若允许并发>1或要求 app process death 后恢复观察，再另审持久化 service |
| KSU 清理 | 删除 source+git 历史恢复；quarantine 继续编译约7页面/3 repo/4 stub | 删除后新增成本0；quarantine 每次架构审查持续覆盖 | 删除；component 只保留 ledger 中可复用能力 |

## 7. 兼容、失败恢复与完成定义

- 保留 prefs：`ui_mode/color_mode/key_color/color_style/color_spec`、导航/页面、`selected_cve` 与 backend config；stale/unknown 值容忍+具名诊断。KSU-only key 不再读、不改 schema。
- 不准备源码 rollback artifact、baseline/rollback fixture 或回滚演练；最终树直接记录 source SHA、exact command/input/literal/exit 与设备证据。Git 历史与 Time Machine 只由用户/owner 用作恢复来源，不授权智能体执行禁用的 git 写命令。
- 同批连续两次失败即停 debug；真实门失败不得进入 D6。完成=G0/G1、D1-D6、两 production backend 冷机门、UML/历史指针全绿；只完成代码或 fixture 不算完成。

## 8. 当前强制越权状态

用户追加的 V0.12 靠底对齐修订已完成；2026-11-03 用户明确知情强制越过本计划原 D0/G1/critic/逐批批准 gate，直接推进 D1-D6 与 registry amendment。已实施范围、compile/inventory 证据和未偿还 M5a/AVD/APK/真机门见 `docs/analysis/kotlin-clean-architecture-d0.md`；该越权不等于修改完成定义，也不把未跑门禁改写为 PASS。

## 9. D7 amendment：清除 frontend/backend 交织，只暴露标准化 BackendSpec

> 状态：2026-11-03 design-critic 对 D7-01..09 逐项复评后阻断 0；用户重复要求“把残留的后端和前端交织代码清掉，只留标准化 BackendSpec”已作为本 D7 目标批准。允许按 D7.1→D7.2→D7.3+D7.4→D7.5 推进，但不把批准解释成允许绕过逐批 AVD/真机门。

### 9.1 问题、目标与唯一边界

当前剩余三类倒置：配置 adapter 直接按 concrete backend 分支；`backend/backends/cve43499/**` 反向导入 `data/cve43499/A301SoDebugConfig.kt`；`core/finish/**` 持有 cve43499 request factory 或导入 concrete cve43499 finish 类型。D7 只整理这些 owner/contract，不调参数、不改攻击算法。

`BackendSpec` 选择为 **Android outer plugin contract**，不是 neutral domain type。它继续拥有每个 backend 唯一 object、stable `key`、flow/config metadata；`Backends.all` 继续是唯一 membership authority。现有 `Context`、`@StringRes`、`R`、`FlowSymbol` 可留在 outer contract；D7 不以“纯化 BackendSpec”为由扩大成全仓平台抽离。

| package/层 | 可依赖 | D7 后禁止 |
|---|---|---|
| `domain/**` | Kotlin/JDK、neutral DTO/port/use case | Android/Compose、`backend.*`、`data.*`、`core.*` |
| `ui/**` | domain DTO/port、UI kit | `BackendSpec`、`backend.backends.*`、data/core concrete |
| `data/backend/**` | domain、`backend.declarations.BackendSpec`、captured registry list/store | `backend.backends.*` concrete class/object、按 key/class 分支 |
| `core/**` | core-local contracts | `backend.backends.*` concrete implementation |
| `backend/declarations/**` | Android outer metadata、domain DTO、现有 core symbol | UI/data concrete implementation |
| `backend/backends/**` | declarations、domain DTO、core/JNI；自身 private config/request | `ui/**`、`data.cve*` |
| composition root | registry/snapshot、ports/adapters | concrete backend object 点名或第二 registry |

~~~mermaid
flowchart LR
  UI[ui semantic renderer] --> DOM[neutral domain DTO and port]
  DATA[data backend adapter] --> DOM
  DATA --> SPEC[BackendSpec outer contract]
  ROOT[composition root] --> SNAP[captured registry snapshot]
  SNAP --> SPEC
  B1[backend 43284 object] --> SPEC
  B2[backend 43499 object] --> SPEC
  B3[backend custom object] --> SPEC
  B2 --> CFG[cve43499 private debug config]
  B2 --> REQ[cve43499 private request factory]
  REQ --> CORE[generic core finish state and payload contracts]
~~~

上述实现箭头固定为 concrete backend → `BackendSpec`。UI/domain 不持有 `BackendSpec`；data adapter 可以持有 captured `List<BackendSpec>`，但只调用标准能力，不能 import concrete object。

### 9.2 标准 configuration contract

只新增一个 declaration capability，不新增 registry/map：

```kotlin
interface BackendConfigurationSpec {
    fun snapshot(store: BackendConfigStore): BackendConfiguration
    fun mutate(
        store: BackendConfigStore,
        mutation: BackendConfigurationMutation,
    ): BackendConfigMutationResult
}

interface BackendSpec {
    val configurationSpec: BackendConfigurationSpec?
        get() = null
}
```

| 现有/新增方法 | 去留与唯一职责 | 副作用/失败 |
|---|---|---|
| `BackendSpec.rows()` | 保留，只是无交互 backend 的 static facts；generic adapter 解析资源后生成 `Static` | 只读 |
| `saveConfig/restoreConfig` | 保留，只服务 session 启动/切换 lifecycle；不是 UI mutation 入口 | typed store 失败抛出，session owner 记录并 fail-closed |
| `configurationSpec.snapshot(store)` | 新增；有交互 backend 的唯一 semantic snapshot projection | 只读 runtime cache/store/provider；异常由 adapter 变成该次 refresh 无更新 |
| `configurationSpec.mutate(store, command)` | 新增；backend 字段规则与 runtime mirror 更新的唯一入口 | `APPLIED/INVALID/UNSUPPORTED`；异常由 adapter 统一变成 `INVALID` |
| port `setBoolean/selectValue/importDocument` | 保留兼容 façade；只构造 neutral `BackendConfigurationMutation` 并调用同一个 private `mutate` | 仅 `APPLIED` 后 refresh 恰好一次 |
| adapter `refresh` | captured key lookup 后调用 capability snapshot；无 capability 才由 `rows()` 生成 `Static` | unknown key/exception 保持原 map，不写持久化 |

neutral DTO 必须携带 renderer 的全部语义，不得把 backend discriminator 藏在 subtype 中：

- `Toggle`：field、title、enabled、enabled/disabled summary、semantic icon token；
- `ChoiceDocument`：field、title、当前 summary、完整 `value+label+kind` option、selected index、import title/summary、MIME list、invalid/error text；UI 不解析 `builtin:`/`uri:`；
- `Document`：field、import title、document/empty summary、MIME list、read-only facts、可选 details；UI 不写 Lua 文案；
- `Static`：已解析 label/value。

UI 只把 semantic icon token 映射到现有 icon，并把 DTO 转成现有 `ConfigItem`；`Backend43284ConfigCard`、`Backend43499GluedProfileCard`、`CustomLuaConfig` 分别 1:1 迁名为 `BackendToggleConfigCard`、`BackendChoiceDocumentConfigCard`、`BackendDocumentConfigCard`。现有 geometry、字符串实际值、Security icon、三组 MIME、picker、点击、error 布局和两套 skin 均冻结；旧 concrete 名称/API 不留 alias。

#### 状态权威

| 字段 | 唯一物理持久化 owner | backend runtime mirror | UI |
|---|---|---|---|
| 43284 `kernel_access_after_relay` | `FinishConfig` 的 `ghostlock.finish/state` 是 canonical physical authority | `FinishConfig.state.kernelAccessAfterRelay` 是同一 owner 的内存态；旧 typed key 只作一次性迁移输入，迁后清除且不再正常读取/写入 | `Toggle` 派生快照 |
| 43499 `profile_reference` | 同一 typed store | `Backend43499ProfileState`，保存 `builtin:<release>`/`uri:<uri>` | `ChoiceDocument` 派生，不解析编码 |
| Custom `script_uri` | 同一 typed store | `LuaScriptSelectionStore.state` | `Document` 派生 |

43499/Lua 的 backend 只拥有字段规则与 runtime mirror，`BackendSessionStore`/typed SharedPreferences store 是这两字段的唯一物理 owner。43284 因该值是完整 `FinishState`/payload 合同的一部分，选择 `FinishConfig` 为唯一 canonical owner；D7.2 的 `restoreConfig` 仅在旧 typed key 存在时执行一次 `typed value → FinishConfig.update → typed store.clear`，`saveConfig` 不再回填 typed duplicate，capability 正常读写只走 `FinishConfig`。迁移覆盖 typed=true/false/absent 三态；成功后 typed key 必须 absent。不得新增第二 preference、map、registry 或 UI cache authority。

#### mutation exact 结果

| 输入 | 结果 | persistence/runtime/permission/refresh |
|---|---|---|
| unknown backend key | `UNSUPPORTED` | 全部不变；0 refresh |
| known key + unknown field 或错误 command subtype | `UNSUPPORTED` | 全部不变；0 refresh |
| 43284 boolean true/false | `APPLIED` | `FinishConfig.load`→`FinishConfig.update`（canonical owner，保持现有内存先更新 + SharedPreferences `apply` 语义）；不写 typed store；1 refresh。调用抛异常才 `INVALID`，此时不 refresh；Android `apply` 无同步 commit 结果，不伪造失败判据 |
| 43499 select null/blank | `APPLIED` | store 写空、runtime clear；1 refresh |
| 43499 known built-in | `APPLIED` | 先 resolve/validate，再 store+runtime；1 refresh |
| 43499 unknown built-in | `INVALID` | store/runtime 不变；0 refresh |
| document URI blank/malformed | `INVALID` | 不请求 permission、不写；0 refresh |
| URI permission failure | `INVALID` | store/runtime 不变；permission 由平台决定；0 refresh |
| 43499 validation failure | `INVALID` | permission 可能已取得（保持既有顺序），store/runtime 不变；0 refresh |
| Lua selection/read failure | `INVALID` | permission 可能已取得，store/runtime 不变；0 refresh |
| 43499/Lua typed store commit failure | `INVALID` | 对这两字段，runtime mirror 更新排在成功 commit 之后；0 refresh。该条不适用于 canonical owner 为 `FinishConfig` 的 43284 |

### 9.3 CBN 与技术债

| 决策 | 方案 A | 方案 B | 当前数字/边际成本 | 选择与翻转条件 |
|---|---|---|---|---|
| BackendSpec 平台边界 | A=保持 Android outer contract；不搬现有 Context/R/FlowSymbol | B=纯化 contract，至少拆 BackendSpec、6 built-in、flow/config adapter 与 resource mapper，>=10 production files | 当前 6 built-in、3 个 interactive config；A 每新 backend=1 object+1 registry entry，data/UI 0 concrete 触点；B 每新增 Android metadata 还需 mapper >=1 处 | 选 A；只有 backend declarations 被抽成独立非 Android module 时转 B |
| config 能力形态 | A=BackendSpec 直接新增 snapshot/mutate 两方法，6 object 都出现默认 API | B=`configurationSpec?` capability，只有 3 configured object 实现 | 当前 A/B 均需 1 declaration；A 每新无配置 backend 暴露 2 无意义方法，B=0；两者每新 configured backend 均 1 capability+1 registry entry；adapter/UI 0 | 选 B；若 6/6 backend 都有同构 config，才折回 base methods |
| config UI | A=DTO 完整携带 semantic values；UI 4 generic renderer | B=按 key/class renderer registry | A 新 backend复用现有 kind=UI 0、新 mutation kind=domain+generic renderer+kit/test 3-4 处；B 每 backend 至少 backend+adapter+UI map=3 处且形成第二 registry | 选 A；只有出现无法由现有四类表达且至少 2 backend 复用的交互时新增一种 neutral kind |
| cve43499 config 移动 | A=本批 1515 行 1:1 move+3 个 owner rename | B=同时拆 model/parser/loader/mapper 4+ 文件 | A 算法编辑为 0，仅 import/package/name；B 会触碰约 30 类型与多段算法边界 | 选 A 以最小化攻击风险；当第二个 CVE 真实复用 parser/model 且有真机预算时另立拆分批 |

未来可能新增 Text/Number/MultiDocument mutation；预估债务是修改 sealed command、DTO、generic renderer 与 tests 各 1 处（3-4 处），不允许用 key switch。偿还触发点是第二个 backend 出现同一种新交互；届时先提取 neutral kind，再接 backend，仍不新增 renderer registry。`Cve43499DebugConfig` 的多职责债务保留到“第二 consumer 复用 + 同批真机门”时偿还，不在 owner 移动中顺手拆分。

### 9.4 exact 文件与符号 disposition

#### D7.1 contract + gate

- 修改 `domain/backend/BackendConfiguration.kt`：完整 semantic DTO、`BackendConfigurationMutation`、结果表；保持 pure Kotlin。
- 新增 `backend/declarations/BackendConfigurationSpec.kt`；修改 `BackendSpec.kt` 加 nullable capability。
- 修改 `tools/kotlin_architecture_d0.py` 与 architecture tests/manifest：独立计数 `ui→backend`、`data/backend→backend.backends`、`core→backend.backends`、`backend/backends→data.cve*`，并拒绝 missing/duplicate/stale/unauthorized target；不加白名单。
- D1 极端输入：空 key、unknown key/field、错误 command subtype、null/blank/超长 option、blank/malformed URI、provider/permission/validation/commit exception；逐项按 §9.2 表断言。

#### D7.2 adapter + generic semantic UI

- 修改 `AndroidBackendConfigurationAdapter.kt` 为 captured `BackendSpec` lookup + capability delegate；删除全部 concrete imports、display-name/codec/store special-case。
- 三个 interactive backend object 实现 capability；43499/Lua 的 preference key/default/reference 编码、validation 与权限顺序不变。43284 按 §9.2 将旧 typed duplicate 做一次性 `true/false → FinishConfig → clear` 迁移，正常路径只读写 canonical `FinishConfig`；这是本批唯一获批的 persistence owner 修正。
- 1:1 rename：
  - `ui/backend/Backend43284ConfigCard.kt` → `BackendToggleConfigCard.kt`；
  - `ui/backend/Backend43499GluedProfileCard.kt` → `BackendChoiceDocumentConfigCard.kt`；
  - `ui/component/backend/CustomLuaConfig.kt` → `ui/backend/BackendDocumentConfigCard.kt`。
- `BackendConfigRenderer.kt` 只按 neutral subtype 分派。component API/file-action manifest 记录 1:1 rename，不保留 old wrapper/typealias。
- 本批 targeted tests 必含 43284 typed duplicate 的 true/false/absent 三态、迁后 key absent、正常 mutation 不再创建 typed key，以及 43499/Lua commit-before-runtime 失败态。本批 architecture/compile 后，立即在当前 AVD 对 Material/Miuix 的 Static/Toggle/ChoiceDocument/Document、picker 和 Home readiness 做 UI 回归；AVD PASS 后才进入 D7.3。

#### D7.3 cve43499 debug-config owner 1:1 迁移

- 文件唯一映射：`data/cve43499/A301SoDebugConfig.kt` → `backend/backends/cve43499/config/Cve43499DebugConfig.kt`；不在本批拆文件或改算法。
- symbol 唯一映射：
  - `A301SoDebugConfig` → `Cve43499DebugConfig`；
  - `A301SoDebugException` → `Cve43499DebugConfigException`；
  - `A301SoDebugParameters` → `Cve43499DebugParameters`；
  - 其余 `DebugFinishParameters`、`DebugExecutionParameters`、`DebugWriteChainParameters`、`DebugVictimTransport` 等 top-level type 与 `NO_PIN_CPU=-1` 保持原名和值，但 package 唯一迁到 `backend.backends.cve43499.config`。
- 所有 production/unit/androidTest consumer 1:1 改 import；旧 package/file/object/type 名清零，不留 alias/typealias/第二 loader。
- 两份真实 asset 的 parsed DTO 与 derived execution/write-chain DTO 对拍；覆盖 A301SO、Mondrian、unknown/ambiguous identity、release exact/mismatch/blank、missing asset、malformed HOCON、missing/wrong key、optional section 缺失、`NO_PIN_CPU=-1`、mm size 0、allocation+1、floor 32/34、乘法/纳秒换算边界，预期保持现有 test literal/具名异常前缀。

#### D7.4 cve43499 request owner 1:1 迁移

- `core/finish/FinishHandoffAdapter.kt` 整文件迁为 `backend/backends/cve43499/request/Cve43499RequestFactory.kt`；package 改为 backend request，`FinishHandoffAdapter` 1:1 改名 `Cve43499FinishHandoffBuilder`，`Cve43499RequestBuild/Factory` 名称保持。它们没有其它 production caller，request factory 自身是唯一 handoff builder caller。
- `backend/backends/cve43499/finish/FinishHandoff.kt`、`FinishAcceptance.kt`、`FinishExecutionEvidence.kt` 保持原 owner/path；它们本来就是 43499 contract，不伪装成 generic core。
- `core/finish/FinishExecution.kt` 删除：全仓 source caller=0、类型为 `internal`，A1 后已明示 deprecated/no production call；同步改 `VictimFinish` 与 evidence KDoc，不保留 dead compatibility layer。若 caller scan 非 0，本批立即停止而不删除。
- core 最终不得 import `backend.backends.*`；这同时覆盖首轮 critic 指出的 `FinishExecution.kt` 漏项。

request 构造次序冻结为：借用 `Context/FinishState` → load/validate debug DTO → render/write/read `files/finish/run.sh` → `FinishHandoff` copy+SHA → derive execution → generator → victim runtime/target source → planner → `buildFromDebug` → payload/primitive/session → placement/actor setup/write-chain → immutable run spec/request。reason literal、single-route、PI/waiter、payload/JNI、KSU guard、process-limit、fail-stop、finish acceptance 均不改。

| 对象/资源 | 创建者/所有者 | 借用与访问区间 | 终结点/释放者；早退语义 |
|---|---|---|---|
| `Context`、`FinishState` | caller | factory build 全程只借用 | 返回即停止访问；不释放 |
| rendered String、readback bytes | builder local | 写盘/读回/SHA 前 | local/GC；handoff 保存独立 copy |
| `files/finish/run.sh` | `FinishScriptStore` 物理 owner | builder 写读；rooted victim 后续读 | 继续由既有 store/lifecycle 管理；当前早退也不删除，迁移不得偷偷改变 |
| `FinishHandoff` | builder 创建，request/run spec 持有 | buildFromDebug 与 Flow/acceptance 只读 | request/Flow 终结后 GC；byte accessor 每次 copy |
| debug/execution/placement DTO | config/factory value owner | generator/planner/spec 构造期间只读 | return/Flow 终结后 GC |
| payload/primitive/generator/source/planner/verifier | factory/caller 创建，run spec/write-chain 持有 | 构造后由 Flow 借用 | Flow lifecycle 终结；本批不改其 close/disarm |
| victim runtime/session | factory/run spec | attack lifecycle 访问 | 既有 lifecycle 先停止访问→解除关联→回收；本批只搬 package |
| closures (KSU/limits/fail-stop/progress) | factory 捕获，run spec 持有 | 对应既有阶段调用 | run spec 终结；调用次序不变 |

D7.3 与 D7.4 合并成一个不可分割的 attack-owner move gate：先完成 source move 与 host/build，再对同一 production APK/ELF/profile 身份执行 A301SO 冷机、固定 CPU 对、单 route、KernelSU 未加载门并归档一份 device-gate；真机 PASS 后才进入 D7.5。这样不让两个未验证 attack 批次串联，也不把 package move写成 exploit 成功。

#### D7.3+D7.4-N current-tree native gate repair

本子批是 D7.3+D7.4 的验证基础设施修复，不是 D7.5a，也不另立第二套计划。顺序固定为：**current-tree native aggregate → A301SO attack gate → D7.5 final closure**。它只恢复被 2.0.5 删除旧 `core/` 后遗留的失真门禁，不修改 waiter/race/payload/route/exec production source、CVE 参数、ELF 形态或 APK runtime。

##### 基线与旧门处置

- branch=`dev-nocpp`，HEAD=`ffa6497adfc12938133e6338e2f95ddbf2ed54b9` 加 D7 工作树。
- `d2778a1aa72b40cf2eedfa4dd6325541b4d7adbc` 是当前 HEAD 的祖先，并在 2.0.5 明确删除 `core/CMakeLists.txt` 与整棵旧 `core/src`；历史 `nativeTestHost` 的 111 tests + 10 structural checks 都绑定那棵已删除实现。它们不是当前 app-native target 的可执行测试清单，不能通过恢复路径名复活。
- 当前 Android build authority 只有 `app/src/main/cpp/CMakeLists.txt` + `app/build.gradle.kts` 的 AGP `externalNativeBuild`；当前 host-eligible source 则分布在 43499-Glued、`tools/tests` 与既有 avbcheck module。根 Gradle 只负责组合 module gate，不复制 module target/test list。
- 本机实际解析的 AGP `gradle-api-9.4.1.jar` 已确认公开 API 同时包含 `SingleArtifact.MERGED_NATIVE_LIBS`、`Variant.artifacts.get(...)` 与 `androidComponents.sdkComponents.ndkDirectory: Provider<Directory>`；实现必须使用这三个 Provider 边界，不依赖 AGP generated task name 或 `intermediates/` 目录形状。

| 旧门 | 旧对象 | current-tree 处置 | 不退化判据 |
|---|---|---|---|
| `nativeTestHost` 111 tests | 已删除 `core/src/tests` 与 `core/src/pipeline/tests` | 历史实现随 2.0.5 target graph 退役；不伪造 111 数字。改为完整 current-tree eligible inventory 的 33 项：43499-Glued 27、native contracts 5、avbcheck 1 | machine disposition manifest 必须覆盖所有 current-tree `*_test.c/cpp` 与具名 harness；新增候选未处置即失败 |
| 10 structural checks | 只读旧 core 的 pipeline/contract/profile 文件 | 随对应实现退役；D7 Kotlin edge 由 `kotlin_architecture_d0.py`，当前 native test registration 由 module drift guard，Android target graph 由 artifact manifest 分别接管 | 不把旧路径不存在当 PASS；每个 current-tree authority 都有可执行 guard |
| `verifyJniLibs` | 旧三库 `libghostlock43499.so/libghostlock43284.so/libghostlock_jni.so` 与已删除 JNI source dirs | 由 current CMake 自导出的 target/artifact/type/JNI-export manifest + `verifyNativeArtifacts` 取代 | merged arm64 artifact set 必须与 manifest 完全一致；每件非空、AArch64；JNI expected/exported exact match，缺件/多件/串库均失败 |
| `make -C core check/android` | 已删除 Make/CMake tree | `./gradlew nativeTestHost` + `./gradlew nativeBuildAndroid` | 同批更新 `AGENTS.md` §8 与 introduction，保留“旧口径已由 current-tree Gradle task 取代”的沿革 |
| Android compile success | 只证明 AGP target 能退出 0 | AGP `mergeDebugNativeLibs` 后运行 manifest verifier | 默认本批 target graph 必须由 native manifest导出并实得 4 件：general、43499-Glued、43284Glued、bootstrap；名单不在 Gradle 手抄 |

##### current-tree native candidate disposition

实现新增 `tools/tests/native-test-disposition.tsv` 作为 machine authority，schema 固定为 `relative_path<TAB>status<TAB>module<TAB>replacement_gate`。`relative_path` 是以 repo root 为基准、`/` 分隔、无 `.`/`..` 的唯一键；重复 row、同一路径重复候选、未知 status/module、row 指向不存在路径或候选缺 row 都 fail-closed。scanner 先构造 set：递归收集 `app/src/main/cpp/**`、`tools/**` 的 `*_test.c|cc|cpp` **与** `test_*.c|cc|cpp`（因此 suffix-style `backend_dataflow_test.cpp` 与 prefix-style `tools/avbcheck/tests/test_avbcheck.cpp` 均由 pattern 收集，不再二次补录），再加入 `tools/**`、backend 根/dev/timer 位置的具名 `*_probe.c|cpp` patterns；module registration 与 host status 必须 1:1 对拍。验收必须定点证明删除 avbcheck 的 TSV row 后 `verifyNativeTestInventory` 非零，再恢复该 row 并确认无 source/build 残留。

| 候选 | 数量 | 处置 | 依据/替代门 |
|---|---:|---|---|
| `backends/43499glued/tests/*_test.cpp|c` | 26 | `host-ctest:43499glued` | 现存 fixed-vector/lifecycle tests |
| `backends/43499glued/tests/host/backend_dataflow_test.cpp` | 1 | `host-ctest:43499glued` | side-effect stub harness；不执行真实 payload |
| `tools/tests/{cpu_pin,placement_floor,pselect_timeout,victim_fd_rules}_test.c` | 4 | `host-ctest:native-contracts` | 文件 KDoc 明示 macOS host/pure contract |
| `tools/tests/gl_log_test.c` | 1 | `host-ctest:native-contracts` | host logger contract；CMake 只对 `gl_log.c` object 注入 write seam，test TU 不改写 libc `write` |
| `tools/avbcheck/tests/test_avbcheck.cpp` | 1 | `existing-host-ctest:avbcheck` | 既有 `avbcheck` typed Gradle task + CTest |
| `tools/tests/glk_log_test.c` | 1 | `retired-orphan` | `glk_log.h`/implementation 已不在当前树；保留具名技术债，不冒充可执行 test |
| `tools/tests/kernelsnitch_hash_test.c` | 1 | `superseded-stale-api` | 旧 C API 已被 current C++ `futex_hash_test` + `kernelsnitch_scan_bounds_test` 覆盖 |
| `tools/tests/perf_probe.c` | 1 | `manual-linux-probe` | 依赖 Linux perf syscall，结果取决于 host policy，不是 deterministic host test |
| `backends/98260/tools/{stream_test,trace_leak_test}.c` | 2 | `manual-device-diagnostic` | 前者只打印 socket write，后者依赖 privileged kernel tracing；均无 host gate 判据 |
| `tools/{abi_layout_probe.c,perf_task_probe.cpp}` | 2 | `manual-diagnostic` | probe，不以 `_test` 命名；由具名实验调用 |
| `backends/43499/{dev/api_probe.cpp,mcast510_probe.c}` | 2 | `android-debug-probe` | dlopen/JNI/Linux race probe；由 Android/device gate 管理 |
| `backends/98260/timer/core_state_probe.c` | 1 | `android-experiment-target` | 98260 CMake selector 的设备实验 TU，不是 host unit |

因此本批 `nativeTestHost` 的冻结总数是 **33**。manifest 的 excluded status 不是 PASS：它只使“为何未进入 host CTest”机器可见。本节表格只是 N1 seed snapshot；N2 生成 TSV 后，TSV 是唯一 machine authority。D7 完成后的普通 candidate 新增/删除只改 TSV 与对应 module registration；只有 status schema、module ownership 或 replacement-gate 语义改变才另立 amendment/ADR，历史 D7 计划不继续充当第二份 registry。

##### CBN、选择与扩展规则

| 方案 | 修改面/维护点 | 风险 | 结论 |
|---|---|---|---|
| A. 复用 Android app CMake 做 host | 1 个入口，但至少要给 common、glgeneral、43499-Glued、43284-Glued 及 Android `log`/ASM/JNI 分支加 host 条件；会把 host policy 写进 production target graph | 触碰 4 个默认 artifact 的生产编译路径，最容易把“能 host 编译”误当 Android 等价 | 拒绝 |
| B. backend/module 自有 host CMake，root Gradle 组合 | 当前 2 个新 host CMake + 既有 avbcheck；每个 module 自有 target/TU mapping，root 只增加一个 dependency | 少量手工 module composition，但不污染 production target | **采用** |
| C. 生成器从 metadata 生成全局 CMake registry | 需新增 schema、generator、generated CMake 与 bootstrap gate；当前只有 3 个 host module | 为尚不存在的规模引入第二构建阶段与 generated source authority | 暂不采用 |

扩展规则：backend 自有 host CMake 只拥有本 backend；`tools/tests` 只拥有跨 backend pure contracts；root `nativeTestHost` 只组合 module tasks。extractor 在 G0/G1 后继续使用已冻结的 `verifyKmp`/decoder gate，root `checkAll` 只追加该 gate dependency，不把 extractor test 塞进 43499-Glued CMake，也不新建第二个 `checkAll`。手工 module aggregation 登记为 F2 技术债；当出现第 4 个 native host module（extractor 的 `verifyKmp` 不计入 native module）或同一 module list 需要在第二处重复时，必须先立 generated registry ADR，再新增 module。

##### 逐文件改动

1. `app/src/main/cpp/backends/43499glued/tests/CMakeLists.txt`（新增）：注册 26 top-level tests + 1 data-flow harness；每项使用历史已验证的最小 production TU/stub mapping；C++23、assert 启用、严格 warning、Threads；module-local glob 与注册表对拍。
2. `tools/tests/CMakeLists.txt`（新增）：注册 5 个 pure/current contract tests；`gl_log.c` write seam 只作用于单独 object target。
3. `tools/tests/native-test-disposition.tsv`（新增）：上述完整 current-tree candidate machine authority。
4. `app/src/main/cpp/CMakeLists.txt`：在所有 include 之后，从当前 CMake target graph 生成稳定 target/artifact/type/source-derived JNI export manifest；不手抄 enabled backend artifact list，不改变 target/link flags。
5. `app/build.gradle.kts`：把 stable manifest output path 作为 CMake argument；在 debug variant 内注册 typed `verifyDebugNativeArtifacts`，输入通过 `variant.artifacts.get(SingleArtifact.MERGED_NATIVE_LIBS)` Provider 取得，`llvm-readelf/llvm-nm` 从同一 `androidComponents.sdkComponents.ndkDirectory` Provider 派生；另注册 exact-four-bucket `verifyDebugUnitTestXml`。不复制 artifact list，不读取硬编码 AGP intermediate path/task name。
6. `build.gradle.kts`：新增两个 typed module host task（各自在一个 `@TaskAction` 内 configure→build→CTest）、`verifyNativeTestInventory` 与 global `nativeTestHost/nativeBuildAndroid/checkAll`；既有 `AvbcheckHostTask` 改为注入统一 tool paths 并加入 aggregate。root Android lifecycle 只依赖 app 自有 public task `:app:verifyDebugNativeArtifacts`，不读取 app variant artifact/path。
7. `AGENTS.md` §8：以 exact Gradle commands 取代已失效的 `make -C core`，保留 replacement 沿革；其它真机判据不变。
8. `docs/development/introduction.md`：删除不存在的 `nativeConfigureHost/nativeBuildHost`/`build/host ghostlock` 口径，写 current module aggregate 与产物位置。
9. `docs/development/full-process-uml.md`：增加 module host CMake→module task→root `nativeTestHost`，以及 app CMake manifest→AGP merge→artifact verifier→`nativeBuildAndroid` 两条不可互替链。
10. `docs/plan/kotlin-clean-architecture-ui-plan.md`、`docs/analysis/kotlin-clean-architecture-d0.md`、`docs/analysis/architecture-review-log.md`：唯一计划、checkbox、critic closure 与验证证据。

##### task DAG、增量合同与失败语义

- `nativeTest43499GluedHost`、`nativeTestContractsHost` 均为 typed task；`@InputFiles` 覆盖 module CMake、disposition、完整 tests/stubs/production TU/headers，relative path sensitivity；`@Input` 冻结 CMake/CTest/Ninja executable absolute path 与固定 option list；`@OutputDirectory` 分别为 `build/cmake/host-43499glued`、`build/cmake/host-contracts`。一个 task 独占一个 CMake tree，并在单个 action 内按 configure→build→CTest 顺序执行，避免两个 Gradle output owner 写同一 tree。
- 两个 module task 与既有 `AvbcheckHostTask` 的 CMake/CTest/Ninja executable 都由根既有 `resolveTool` 在配置期解析后注入 typed `@Input`，action 内禁止退回字面量命令；options 同为 tracked `@Input`。三者都设置 `outputs.upToDateWhen { false }`：普通第二次 `./gradlew nativeTestHost` 仍真实 configure/build/CTest，不依赖 `--rerun-tasks` 才正确。source 新增/删除同时先被 global disposition 与 module drift guard捕获。
- `nativeTestHost` 是无 output 的 lifecycle aggregate：dependsOn `verifyNativeTestInventory`、两个 module task、`avbcheck`；任一非零即非零，成功必须汇总 33/33、failed=0。
- app `verifyDebugNativeArtifacts` 通过 `SingleArtifact.MERGED_NATIVE_LIBS` Provider 自动取得 variant producer dependency与 merged input；同一 app Android Components 的 `sdkComponents.ndkDirectory` Provider 派生并注入 `llvm-readelf/llvm-nm` 两个 `@InputFile`。它读取 CMake stable manifest，检查 exact set、regular/nonempty、AArch64、JNI export exactness；不手写 `mergeDebugNativeLibs`、AGP intermediate path或另一套 NDK resolver。root `nativeBuildAndroid` 只 dependsOn `:app:verifyDebugNativeArtifacts`。
- app `verifyDebugUnitTestXml` dependsOn `testDebugUnitTest`，全量读取该 variant 的 `build/test-results/testDebugUnitTest/**/*.xml`；XML 相对路径与 suite name 都必须唯一，缺 XML/重复 suite/解析失败或 aggregate 不等于 `tests=459, failures=0, errors=0, skipped=0` 均非零。四个 expected 值是 typed `@Input`，在 app task registration 唯一写入。root `checkAll` dependsOn `nativeTestHost`、`nativeBuildAndroid`、`:app:verifyDebugUnitTestXml`，不直接以裸 test task替代 XML gate。N2 前若共享 writer 合法新增/删除测试，只允许一次具名重基线并附 source diff，同时更新 app task 的唯一 expected input 与本段数字，不能用“实际全量”移动判据。
- 所有输出只在仓内真实 `build/`；不创建 rollback/baseline fixture、独立 shell、source-tree binary 或链接。configure/build/CTest/inventory/manifest/nm/readelf 任一步非零即 fail-closed。

##### 验证矩阵与批准门

1. design-critic 对本活跃计划复评阻断归零，用户批准本修订后才实现。
2. `./gradlew tasks --all --console=plain` 必须列出 module/global/inventory/artifact tasks。
3. `./gradlew nativeTestHost --console=plain` 连续普通运行两次；每次真实执行三个 module CTest，汇总 33/33、failed=0；不靠 `--rerun-tasks`。
4. `./gradlew nativeBuildAndroid --console=plain` 必须实际完成 AGP configure/build/merge + artifact verifier；manifest/merged set exact，默认 4/4、AArch64、JNI exact、compile output 新 warning=0。
5. `./gradlew checkAll --console=plain`；host 33/33、Kotlin XML 459/0/0/0、Android artifact 4/4，exit=0。
6. 重跑 D7 inventory/verify、stale owner scan、debug/androidTest APK 与 `git diff --check`。软件门全部绿后才连接 A301SO；同一 production APK/ELF/profile 执行冷机、固定 CPU 对、单 route、KernelSU 未加载的单次 `Real43499GateTest`，归档原始日志/恢复态。
7. A301SO gate PASS 后才进入 D7.5；native aggregate 或设备门任一缺失都不得把 D7 标完成。同批连续失败两次停回 debug。

##### D7.3+D7.4-N 实时 checkbox（2026-11-04）

- [x] **N0 设计/批准**：固定 design-critic 最终总判“可进入用户批准 gate”，阻断 0、重要 0；用户随后以“批准”授权本 exact 子批。
- [x] **N1 inventory authority**：`native-test-disposition.tsv` 覆盖 43 个 live candidate；host 分组固定为 27 + 5 + 1。删除 avbcheck row 时 `verifyNativeTestInventory` exit=1、literal=`missing_rows=[tools/avbcheck/tests/test_avbcheck.cpp]`；恢复后 `CANDIDATES=43 HOST=33`/exit=0，未留下临时 source/build entry。
- [x] **N2 module host CMake**：43499-Glued 的 27 项最小 TU/stub mapping 与 native-contracts 5 项已注册；module discovery/TSV drift guard、C++23/C11、assert、warning-as-error、Threads 与 Darwin/ELF dead-strip 分支均落地。
- [x] **N3 root Gradle aggregate**：三个 module task 均注入 typed CMake/CTest/Ninja inputs并 always-run；最终树普通连续两次 `./gradlew nativeTestHost --console=plain` 均实际 configure/build/CTest，literal=`NATIVE_TEST_HOST=PASS TOTAL=33 FAILED=0`、exit=0。
- [x] **N4 Android artifact gate**：CMake 从当前 target/source graph 自导出 manifest；`./gradlew nativeBuildAndroid --console=plain` literal=`NATIVE_ARTIFACTS=PASS COUNT=4 ABI=arm64-v8a JNI=6`、exit=0。
- [x] **N5 Kotlin XML gate**：`:app:verifyDebugUnitTestXml` 全量解析 76 份 suite，literal=`UNIT_TEST_XML=PASS FILES=76 TESTS=459 FAILURES=0 ERRORS=0 SKIPPED=0`、exit=0。
- [x] **N6 software aggregate/docs**：最终 verifier hardening 后的 `./gradlew checkAll --console=plain` 同树输出 host 33、Android artifact 4/AArch64/JNI exact 与 XML 459/0/0/0，literal=`BUILD SUCCESSFUL in 41s`、exit=0；随后两次最终普通 host invocation 分别为 `BUILD SUCCESSFUL in 1s` 与 `979ms`。`AGENTS.md` §8、introduction 与本 UML 已更新到 current-tree authority。
- [ ] **A301SO attack gate（不可由 N0-N6 替代）**：仍需同一 production APK/ELF/profile 的冷机、固定 CPU 对、single route、KernelSU 未加载 `Real43499GateTest` 与 `device-gates/` 原始日志；未通过前 D7.3+D7.4 与 D7.5 不置完成。

#### D7.5 authority/document final

- machine rows 1:1 处置：旧 A301 config、FinishHandoffAdapter、FinishExecution、三个 card rows 分别标 D7 move/delete；新 config/request/card path 为唯一 target。`FinishHandoff` row 保持 existing owner。
- KSU KEEP ledger 中原 `FinishHandoffAdapter.kt` 的 5 条 literal 内容/数量不变，只迁 owner/path 到新 request factory；Annex A 原 owner 同批改指针。component API manifest 记录 `CustomLuaConfig` → `BackendDocumentConfigCard` 1:1 rename。
- 更新 `full-process-uml.md` 为最终结构唯一权威图；本文完成后只保留到该图的指针。同步 README/README_ZH、inventory 与 review log。

### 9.5 批次门、命令与完成定义

1. **D7.1**：targeted contract/architecture test + `:app:compileDebugKotlin` + `git diff --check`；输入含 §9.2 极端值，expected 为结果表 literal 与四条新 forbidden-edge count。
2. **D7.2**：三类 mutation targeted tests + architecture + compile；随后 AVD Material/Miuix 四种 DTO、picker、Home readiness。AVD 未绿不得进入 attack move。
3. **D7.3+D7.4-N**：先按本节修复并跑绿 current-tree `nativeTestHost/nativeBuildAndroid/checkAll`，取代不存在的 `core check/android`；不得以 Android build 成功替代 host gate，也不得以 43499-Glued 局部 CTest冒充 root aggregate。
4. **D7.3+D7.4 attack gate**：两 asset parser/derived DTO、request factory/finish contract targeted tests与上述 native aggregate 都绿后，核验 production 身份并执行 A301SO 真机门。真机未绿不得进入 D7.5。
5. **D7.5**：inventory/manifest stale+duplicate+missing gate、full unit/build/current-tree native aggregate 复跑、UML/README/review log 收口；不首次补跑前批功能门。

所有命令的 exact input/literal/exit 与 XML tests/failures/errors/skipped 写入实时记录；同批连续失败两次停回 debug。D7 冻结 registry membership/stable key/flow symbol/selected key/route/AIDL/wire/profile/schema/asset 内容/偏移值/payload/JNI/native/PI/waiter/finish behavior、`LegacyProfileConverter.kt` 与 `kernelsnitch/`。

D7 完成＝UI/domain/core concrete backend 边归零、data adapter 不见 concrete backend、backend 不反向 import data/UI、只有 standard `BackendSpec.configurationSpec` 暴露配置能力、旧 owner/name/API/manifest row 无残留、D7.1/D7.2/attack device/final 门逐批闭环。仅源码移动或编译通过不得写完成。
