# Kotlin Clean Architecture 计划附件 A：执行数据

> 从属于 `kotlin-clean-architecture-ui-plan.md` v5；只承载路径、依赖、所有权和边界，不新增架构决策。任何冲突以主计划为准。

## 1. D0 唯一写范围与 machine manifests

D0 只允许以下新增与明确修改；`app/src/main` 源代码只读：

- `app/src/test/kotlin/com/ghostlock/app/architecture/ArchitectureInventoryTest.kt`
- `app/src/test/kotlin/com/ghostlock/app/architecture/ComponentApiManifestTest.kt`
- `app/src/test/kotlin/com/ghostlock/app/architecture/KsuBoundaryLedgerTest.kt`
- `app/src/test/kotlin/com/ghostlock/app/architecture/UiSurfaceManifestTest.kt`
- `app/src/androidTest/kotlin/com/ghostlock/app/architecture/UiBaselineCaptureTest.kt`
- `app/src/androidTest/kotlin/com/ghostlock/app/architecture/UiBaselineFixtures.kt`（D0 只向现有 production composable 提供普通输入/slot；不得复制 production renderer、不得构造 Run M5a 状态、不得启动真实攻击）
- `tools/kotlin_architecture_d0.py`（inventory/verify/capture/pack 唯一运维入口）
- `app/src/test/resources/architecture/kotlin-file-actions-v1.tsv`
- `app/src/test/resources/architecture/component-api-v1.tsv`
- `app/src/test/resources/architecture/ksu-ui-remove-v1.tsv`
- `app/src/test/resources/architecture/ksu-exec-keep-v1.tsv`
- `app/src/test/resources/architecture/ui-surface-matrix-v1.tsv`
- `app/src/test/resources/architecture/ui-goldens-v1.tar`（D0 含 68 个当前可达 production screenshot + semantics；surface TSV 另含 20 个 `deferred-D5` M5a 行，D5d 形成 production renderer seam 后补齐并重打包为 88；成员名/hash 均由 surface TSV 锁定）
- 修改 `app/build.gradle.kts`：以现有 `testImplementation("androidx.compose.ui:ui-test-junit4:1.12.1")` 为语义锚点，仅新增逐字两行 `androidTestImplementation("androidx.compose.ui:ui-test-junit4:1.12.1")` 与 `debugImplementation("androidx.compose.ui:ui-test-manifest:1.12.1")`；不得改 plugin/Compose 版本/release runtime/其它 Gradle 配置
- 修改 `docs/plan/43499-kotlin-package-refactor-plan.md` 头部：仅标 M5 后冻结/待重基线，不声明本 amendment 生效
- 修改 `docs/analysis/architecture-review-log.md`：追加 D0/评审一行
- `docs/analysis/kotlin-clean-architecture-d0.md`（只记 identity、命令、结果、未验证；不设字节或篇幅门）
- 最终树证据直接写入上述 D0 记录与 `architecture-review-log.md`；禁止准备 `ROLLBACK.sh`、`MODIFIED_FILE`、`DIFF_FILE`、`VERIFICATION.txt`、baseline/rollback fixture 或隔离回滚演练

file-actions、KSU 与 surface ledger 至少含 `path,symbol/resource,current_owner,action,target,batch,evidence`；mixed file（含 Manifest）按 symbol/literal 单独记 SHA，不得用 whole-file KEEP 掩盖同文件 REMOVE/RENAME。file-actions 覆盖所有待改/删/移/新增生产文件；未列文件=冻结。

component manifest 另用强 schema：71 个 baseline component 文件各一条 file row（只允许 `preserve-path|rename-path-1to1`，不得合并/丢失），且每个 public/internal reusable declaration 恰一条 API row；计划移入 component 的 API 同样登记 source。`api_key=kind|owner_fqn|name|receiver_type|type_parameter_arity|normalized_parameter_types`，不含行号；type/property 另锁 declared type/mutability。字段至少含 `old_signature_sha,action,target_api_key,target_signature,parameter_mapping,default_mapping,caller_manifest,batch,evidence,source_line`，source_line 仅诊断。baseline action=`preserve-exact|rename-1to1|inputize-signature-1to1`；target-only 新 API 仅可 `add-approved` 且 `source=ABSENT`、exact signature/evidence 经 G1 逐签。inputize 逐参数把旧 global/Context/repository 映射到 value/callback/Actions，caller 同批迁移。测试断言 71 baseline file rows、每个非 private API 恰一行、key 唯一、每个 source 恰一 target、target 至多一 source、无未分类/静默删除/合并/未批新增；fixture 同时调用 Composable、构造/引用 type/property/helper。D0 结束记录五份 SHA-256，design-critic 与用户逐份批准后才形成 G1；后续增删一行即计划变更，须重审。

## 2. 生产子批的 manifest 规则

G1 前仅冻结以下语义边界，不授权写入：

| 子批 | 初始锚点；最终以 file-actions TSV exact rows 为准 | 单变量 |
|---|---|---|
| D1a | 新 `domain/{settings,device,backend}` DTO/port/usecase + 对应 unit tests | 只加纯合同，不接生产 |
| D1b | `ui/data/repository/SettingsRepository*.kt` -> `data/settings/SharedPreferencesUiPreferencesRepository.kt` | 一个 prefs adapter，key 不变 |
| D1c | `ui/Kernels.kt` -> `data/device/KernelVersionReader.kt`；`ui/util/SELinuxChecker.kt` -> `data/device/SELinuxStatusReader.kt` | 一个 device adapter；不搬无 caller 的 private `runGetenforce` |
| D1d | `GhostLockApp.kt`、新 `AppContainer.kt`、明确列出的 VM factory | 只接 composition；不改页面 |
| D1e | `ui/BuildConfig.kt` consumers | 只统一 generated BuildConfig |
| D2a | `component/bottombar/{BottomBar,BottomBarMaterial,BottomBarMiuix,NavigationRailMaterial,NavigationRailMiuix}.kt` | badge/destination 与 `fullFeatured/page count` 输入化 |
| D2b | `component/profile/*.kt` + neutral draft；`ui/profile/{Capabilities,Groups}.kt` 移入 component/profile | 去 `Natives.Profile`，catalog/API 1:1 |
| D2c | `component/{markdown/GithubMarkdown,material/SendLogBottomSheet,miuix/SendLogDialog,rebootlistpopup/{RebootListPopup,RebootListPopupMaterial,RebootListPopupMiuix},KsuValidCheck}.kt` + `ui/util/LogEvent.kt` + `ui/screen/settings/{SettingsMaterial,SettingsMiuix}.kt` | asset/bugreport/reboot capability 输入化；caller 同批 |
| D3a | `navigation3/Routes.kt`、`MainActivity.kt`；`IntentDispatcher.kt -> ABSENT` | 先断 KSU route/deep-link/ZIP；neutral action=0 才删 dispatcher |
| D3b | KSU REMOVE ledger 中 service/AIDL/screen/repo/VM/util rows | 只删已不可达 source |
| D3c | `screen/home/*`、`HomeViewModel.kt` | 只替换 Home 产品模型 |
| D3d | `screen/settings/*`、`SettingsViewModel.kt`、settings port/adapter | 只删假 KSU setting |
| D3e | REMOVE ledger 的 resource rows | 只清资源；component/manifest引用保留 |
| D4a-g | Home / Backend / Settings / About / ColorPalette / Finish / Run shell 各自 rows | 每次一页 |
| D5-pre | 新 `app/src/androidTest/kotlin/com/ghostlock/app/backend/core/Real43284GateTest.kt` | 只建双 opt-in 真实门；G1 单独批准，不运行 |
| D5a-e | domain run contract / dispatcher adapter / VM / renderer / registry amendment（含 `FlowEntryRegistryAssembly.kt` 单 snapshot）; D5d seam 绿后补 M5a 20 基线 | 每次一条边界；视觉捕获只调 production renderer、不 dispatch |

D3b 的页面初始集合：`superuser, appprofile, sulog, template, templateeditor, executemoduleaction, scriptrun`；`module` 只删除 `ModuleScreen.kt/ModuleUiState.kt/ShortcutType.kt`，保留 `BackendPage.kt` 与 `backends/Finish*.kt`。最终不得用 glob 作为删除输入，必须由 TSV 逐文件驱动。

D0 对不存在的 production path 只可生成以下 exact ADD/MOVE/RENAME targets（花括号逐字展开为所列 basename，不是 glob）：D1=`domain/settings/{UiPreferences,UiPreferencesPort,ObserveUiPreferencesUseCase}.kt`、`domain/device/{DeviceStatus,DeviceStatusPort,ReadDeviceStatusUseCase}.kt`、`domain/backend/{BackendRuntimeSpec,BackendRegistrySnapshot,BackendCatalogPort,GetBackendCatalogUseCase}.kt`、`data/settings/SharedPreferencesUiPreferencesRepository.kt`、`data/device/{AndroidDeviceStatusAdapter,KernelVersionReader,SELinuxStatusReader}.kt`、根包 `{AppContainer,AppViewModelFactory}.kt`；D2=`ui/component/CapabilityGate.kt`、`ui/component/profile/{ProfileDraft,Capabilities,Groups}.kt`；D5=`domain/run/{RunCommand,RunSessionState,RunSessionPort,RunBackendUseCase}.kt`、`backend/launcher/RunCoordinator.kt`、`data/backend/{BackendCatalogAdapter,BackendPresentationAdapter,BackendRunSessionAdapter}.kt`、`ui/screen/run/{RunUiState,RunActions}.kt`、`ui/viewmodel/RunViewModel.kt`。变更 target path 或新增 production path 都是计划变更，先重审。

theme 映射固定为：Kotlin `KernelSUTheme -> GhostLockAppTheme`、`MaterialKernelSUTheme -> MaterialGhostLockTheme`、`MiuixKernelSUTheme -> MiuixGhostLockTheme`；XML `Theme.KernelSU -> Theme.GhostLock.App`、`.Starting -> Theme.GhostLock.Starting`，无 caller 的 `.WebUI -> ABSENT`。既存 XML `Theme.GhostLock` 保持；dormant Kotlin `GhostlockTheme` 在 caller=0 后单列 DELETE，禁止同名覆盖。

D0 seed 的终态 source delete closure 是 69 Kotlin+1 AIDL（只读扫描的68 Kotlin再加本计划裁定删除的 `IntentDispatcher.kt`），但必须按依赖分配到 D1e/D2/D3/D4，不能整批标 D3b：`BuildConfig` 属 D1e；`Natives/KsuApp/KsuCli` 待全部保留 caller 迁完；dormant theme 待 live theme 门绿；AIDL/service/repository/旧页面只在 D3a 可达性归零后删。exact 70 path 由 TSV 锁定；任何 source/测试/resource 额外 row 都须证据与 G1 批准。

唯一 batch：三个 backend object 的 readiness 文案/状态只归 D3c（D1c 只落 device port/adapter）；BackendSpec、各 object 的 ConfigCard signature 与 `FlowEntryRegistryAssembly` 只归 D5e（D4 Backend shell 把它们当只读）。跨批同 path 可有不同 symbol row，但同 symbol 只能一个 owner batch。

## 3. RunSession Rust 式所有权/生命周期

| 对象 | 创建者/所有者 | 借用者与访问区间 | 终结点/释放者 | 禁止 |
|---|---|---|---|---|
| `RunUiState` | Run VM | renderer 在订阅期间只读 | VM clear 后 state 不再发布 | renderer 写终态 |
| observation job | VM | 只收 progress/log/state | rotation 不终结；VM clear 只 detach observer | 把 detach 等同 host cancel |
| underlying run / `RunCoordinator` | Application-scoped execution adapter | host/Flow 到 terminal/handoff；VM 不拥有物理 run | terminal/handoff 后按现行合同释放；coordinator 清记录 | Activity/VM death 终结物理 run |
| observer/control handle | coordinator 创建；VM 只持 token | VM 在订阅期观察；显式用户 action 才借控制权 | VM clear 仅 detach token；幂等 | 二次 start/close、renderer 持有 |
| launcher binding/callback | App/Shizuku launcher | adapter 在 bind 到 terminal/handoff | 沿现行 launcher plan unbind/remove 规则 | 改 keepalive/remove 语义 |
| log fd/PFD | launcher owner | host/Flow 在 run window 借用 | 先停止访问，再 callback/解绑，再 owner close | UI 先 close、handoff 后读写 |
| Flow/host | host process | callback 观察；Flow 拥有编排 | 现行 terminal/self-kill 或 keepalive | UI 生命周期直接 kill host |
| attack child/payload | Flow/攻击实现转移所有权 | 触发前由 Flow；handoff 后由后继/系统 | 仅现行 Flow 合同定义 | onCleared/cancel 后 kill、wait exit、追加状态 |

状态规则：
- 用户显式 cancel 且 `abortable=true`、不可逆触发前：VM -> use case -> adapter -> 现行 host cancel；顺序保持“停止访问 -> 同步/解除关联 -> 回收”。
- rotation：同一 VM/session，renderer 重订阅；不 cancel。
- VM `onCleared`：只 detach observation。不得自动 cancel 真 run。
- `HandoffRequested` 或触发后：detach/unbind 按现行计划；不 kill、不 timeout、不读 exit、不写后续状态。
- UI death/host death/binder death：沿 `flow-process-launcher-plan` 既有具名终态；adapter 只映射，不发明 fallback。
- 白盒用例：start twice、cancel twice、close twice、rotate Running、clear before/after irreversible、handoff late callback、host death、callback death；每个返回具名结果且无资源二次释放。

## 4. KSU 边界 seed ledger（D0 扩展为逐符号 TSV）

### 4.1 KEEP：真实攻击/交接（集合必须逐字不增不减）

- `app/src/main/AndroidManifest.xml`：`me.weishu.kernelsu`、`me.weishu.kernelsu.pr` package query；Activity theme 名则进入 REMOVE/RENAME。
- `core/finish/FinishConfig.kt`：`RootManagerSpec` 的 KernelSU/Next、`libksud.so`、late-load 参数。
- `backend/backends/cve43499/request/Cve43499RequestFactory.kt`：backend-private root-manager request/handoff builder 与 `KernelsuGuard` adapter；该文件是不纯攻击外层，不进 domain/data/UI。
- `ui/screen/module/backends/FinishUi.kt`：真实 handoff 的 `launchOnly -> safe-mode` capability 分支；保留该分支及 M2 视觉/semantics，不归入管理器页面删除集。
- `core/payload/PayloadEffects.kt`：`/sys/module/kernelsu` 效果验证。
- `core/payload/PayloadRequests.kt`：`ksud`/late-load/safe-mode 脚本。
- `backend/backends/cve43499/owned/KernelsuGuard.kt`：加载前 fail-closed guard。
- `backend/backends/cve43499/{flow/Flow43499Impl.kt,flow/Flow43499RunSpec.kt,finish/FinishAcceptance.kt,finish/FinishExecutionEvidence.kt,w3/Cve43499W3HardGate.kt}`：guard/evidence/finish gate。
- 对应 unit/androidTest 的 guard、preflight、real gate 与 handoff evidence 符号；D0 TSV 逐测试列出，不允许目录 wildcard。
- native handoff/loader 字符串与设备证据属于现有执行计划，D0 只登记 hash，不改。

### 4.2 REMOVE/RENAME：管理器 UI 移植

- `ui/{Natives,KsuApp,KsuService,BuildConfig}.kt`、`aidl/.../IKsuInterface.aidl`、`ui/util/KsuCli.kt`。
- `ui/navigation3/IntentDispatcher.kt` 的 `ksu://`、module ZIP；Routes 的 KSU page keys。
- D3b 七组 screen、其 repository/model/script/profile/VM/util 依赖。
- `Theme.KernelSU*`、`KernelSUTheme`、`rememberKernelSU*`、`KernelSU_bugreport_*` 等 UI branding 改 GhostLock 中性名。
- component 中 `Natives/ksuApp` 依赖进入 D2，不以“component 保留”为由进入 KEEP。
- `BackendCve43499.kt` 等注释中“UI stub”事实在 stub 删除后改为真实 readiness/历史说明；真实 KSU guard 不删。

Gate 不是简单 `rg KernelSU`：`KsuBoundaryLedgerTest` 读取两个 TSV，REMOVE 要求 source symbol/resource 为 0；KEEP 要求 path+symbol+literal hash 与批准集合相同。任何新增命中先分类并再批准。

## 5. Import DAG 机械规则

`ArchitectureInventoryTest` 对 Kotlin package/import 建图，最终断言：

1. `domain/**` 禁 `android.*`、`androidx.*`、`com.ghostlock.app.{attack,backend,core,data,jni,log,shizuku,ui}`。
2. `ui/screen/**` 与 `ui/viewmodel/**` 禁 `com.ghostlock.app.{attack,backend,core,data,jni,log,shizuku}` 及 `com.ghostlock.app.ui.data`；只经 domain/本 UI。
3. `ui/component/**` 禁 `Natives`、`ksuApp`、具体 repository/profile 与 `com.ghostlock.app.ui.data`；不做文件白名单。
4. `data/**` 可依 domain/Android，不依 `ui/screen`、`ui/viewmodel`。
5. 现有 execution adapter 可依 backend/core/jni/shizuku/log 与 domain，不反向被 domain import。
6. composition roots 仅为 `com.ghostlock.app.GhostLockApp`/`AppContainer`、`backend.host.FlowHostService`、`shizuku.GhostlockUserService`，可依对应外层；其它包不得 import具体 adapter implementation；D6 时 `ui/data/**` source 必须为 0，避免旧包绕过第 2 条。

迁移中测试按 batch rows 只检查“已迁集合”；D6 检查全仓，违规计数必须 0。不得用 allowlist 掩盖既有违规。
