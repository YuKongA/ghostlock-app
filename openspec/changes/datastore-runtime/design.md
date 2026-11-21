# typed DataStore设计及Android exact runtime

## Context and scope

完整迁入的有效设计/范围与验收正文；不是新增实施批。原批准/历史结果按原Beads记录，工具工件齐备不代表实现或门禁通过。

来源：`043fdf6a:.beads/issues.jsonl`；保留语义代号和原锚点。本文中的历史进度/门结果文字仅是其具名输入的沿革，当前状态从proposal列出的Beads读取。旧机制由正文已注明的后继差异取代；基线不重新授权实施。

## Decisions

<a id="record-gl-zgp"></a>

## P-SETTINGS-IO-001：DataStore Android exact runtime

<!-- migrated-source: 043fdf6a Beads gl-zgp description -->
本节是迁入OpenSpec的现行设计/验收正文；Beads只保存执行状态、原批准和实际证据。历史来源仅作追溯。
NOT_RUN：source与普通门既有完成记录保留；当前没有获准AVD，等待原批准设备/前提后跑exact instrumentation；JVM结果不替代Android，也不擅自换真机。
原始来源（已归档/承接）：docs/archive/20261121-1301-backend-root-execution-unification-plan.md#p-settings-io-001

<!-- migrated-source: design gl-zgp -->
<!-- source: f882e670:docs/plan/backend-root-execution-unification-plan.md:L2028-L2119 -->
<a id="p-settings-io-001"></a>

##### P-SETTINGS-IO-001/批次01 全项目 typed DataStore 持久化收口（source 实施完成；Android exact runtime 待补）

###### 取代、保留与本批边界

**2026-11-08 用户裁定**：内部持久状态直接使用 `kotlinx.serialization` 序列化完整 Kotlin data class，并由 typed `DataStore<T>` 持有；不再保留“Preferences key 映射 + Payload/Finish 手写 canonical JSON/JCS codec”这两套内部机制。该裁定取代本节上一版的“两份 Preferences DataStore、两份保留 v3 文件格式的 typed DataStore、Hub 全局 mutex”设计；旧 `payload-v3.json`、`finish-v3.json` 的具体内部字节格式和纯内部 `*DocumentV3` wrapper 不再是目标合同。

**SETTINGS-REQ-CONFIG-01 已关闭（2026-11-08 用户规则修订）**：AGENTS §4 现只要求“需要系统内核偏移信息的 backend”以 GLK profile 为配置权威；Diagnose 与 43284 明确不在该集合。`stress_enabled` 因此可以由 Diagnose 自己的 serializable settings data class/typed DataStore 持久化，并在 run 前单向冻结为 immutable snapshot；GLK profile、SharedPreferences、环境变量和 native global 不得镜像该值。该修订是配置分类规则本身的收窄，不是知情越权，也不产生例外技术债。

继续保留的产品边界只有：Payload 与 Finish 仍是两个相互独立、且独立于 backend 的 domain authority；用户发起 Flow 前必须把当前编辑状态真正写入并等待完成；需要系统内核偏移信息的 backend profile 仍是该 backend 自己的 `.conf`/GLK profile 文件；Flow/RunHost 只消费一次冻结的输入，不直接打开应用设置存储。本批已按冻结职责完成 production source；未修改开发者定义任务清单的结构或新增条目。

###### 最小存储规则

1. **DataStore 只保存小型结构化状态**：应用设置、用户选择、不依赖系统内核偏移的 backend 设置、Payload 与 Finish。每个 aggregate 直接序列化自己的完整 data class；repository 只做 domain 校验和 Android 副作用，不再逐 key 搬运字段。需要系统内核偏移的 backend 配置仍进入自己的 GLK profile，不能在 DataStore 镜像。
2. **SettingsHub 只表示 UI 进程内的唯一装配/所有权入口**：它可以持有具名 typed stores，但不得增加 factory、反射注册表、第二缓存、revision、全局 mutex、后台重试队列、数据库或跨 store 事务协议。若直接由具名 repository 唯一持有 store 更简单，实施设计应删除空壳 Hub，而不是为名称保留一层。
3. **DataStore actor 是唯一写顺序 owner**：每次编辑都以 `updateData { current -> current.copy(...) }` 或具名 aggregate transform 直接提交，不从观察流取旧快照后整对象覆盖。一次用户命令仅在 repository 的 application-scoped `async(start = CoroutineStart.UNDISPATCHED)` 已进入对应 `updateData` 调用并返回 completion handle 后才算 `accepted`；因此 callback 返回前，命令已经交给同一个 DataStore actor，后续事件即使首个写仍在途也继续提交，不能静默丢弃。每个 repository 只跟踪未完成 completion handle 与首个写失败，用于 await/fail-closed；它不重排、不重放、不保存数据，也不是第二条 writer queue。不得增加全局 mutex、revision protocol 或“稍后自动落盘”。Payload 与 Finish 分别提交，不声称崩溃时跨文件原子性。
4. **进程边界固定**：UI/application 主进程是 typed DataStore 的唯一 reader/writer。当前 `:flowhost` 以及 Shizuku host 不构造 SettingsHub、不打开同一 DataStore，只接收已经冻结并完成身份绑定的请求/文件。
5. **内部格式不等于导出格式**：DataStore JSON 是实现细节，不能直接作为“导出 conf/备份包”交给用户。后续导出从 domain data class 生成明确的 `.conf`、JSON 或 ZIP 产品格式；修改导出格式不产生第二个运行时 authority。
6. **字节资产不塞进 DataStore**：字体、脚本、LKM、profile、custom backend ZIP/WebUI 文件和日志使用文件存储；DataStore 最多保存稳定 logical id、相对位置或选择状态。绝对 app-private path、短期 picker URI、fd、PID、运行时 token/端口均不进入持久 data class。
7. **启动与失败语义**：缺失 store 使用 data class 默认值；UI 首帧须等主题所需状态完成首次读取，不能先绘默认主题再闪变。可执行配置读取/校验失败时 preview 与 Flow fail closed；普通显示偏好损坏时允许回到默认并记录一条错误。不得静默拿旧缓存继续攻击。
8. **迁移只允许一次且不是双权威**：是否保留当前开发期 SharedPreferences/旧 Payload/Finish 数据在实施设计中作一次明确裁定；若迁移，只做单次 import，成功后 DataStore 是唯一 authority，不保留长期双写、legacy codec 或通用迁移框架。

###### 全项目持久化需求总表

| 数据类别 | 当前实现/位置 | 目标 authority 与介质 | 生命周期、备份和约束 |
|---|---|---|---|
| UI 外观与交互设置 | `ui-preferences.json` 中的完整 `UiPreferences` typed DataStore | `UiPreferences` 整体 typed DataStore | 跨进程重建/重启保留；主题字段首次读取前保持 splash；不由 `:flowhost` 打开。 |
| 日志、运行提示与开机行为设置 | `operational-preferences.json` 中的完整 `OperationalPreferences` typed DataStore | `OperationalPreferences` 整体 typed DataStore | 只保存选择与策略；日志内容、字体字节和运行结果不放入该对象。 |
| 权限执行器选择 | `app-selection.json` 的 `AppSelection.selectedExecutor` | 与 backend 选择共享同一个完整 `AppSelection` typed aggregate，但只以字段 transform 修改 | 只持久化 App/Shizuku 选择；binder 在线、permission、uid 等事实每次实时读取，禁止缓存为 authority。 |
| 当前 backend 选择 | `app-selection.json` 的 `AppSelection.selectedBackendKey`，`BackendSessionStore`只保留运行期投影 | 与执行器选择共享同一个完整 `AppSelection` typed aggregate，但只以字段 transform 修改 | 恢复 stable backend key；key 不存在或不可选时回退当前 registry 首个 selectable。backend membership/descriptor 本身不持久化。 |
| backend 设置 | Diagnose stress 等尚未落地；旧候选错误地强制使用 GLK profile | 不依赖系统内核偏移的 backend 使用自己的 serializable settings data class；需要系统内核偏移的 backend 使用 GLK profile | Diagnose `stress_enabled`以typed DataStore为唯一持久authority并单向派生run snapshot，禁止profile镜像。43284同样不被强制放入GLK。Custom backend当前只允许WebUI，其数据随package自管。 |
| Payload | `payload.json` 中的完整 `PayloadDefinition` typed DataStore | `PayloadDefinition` 整体 typed DataStore | 与 backend/Finish 独立；页面编辑可在内存实时显示，但 preview/Flow 前必须等待 durable save。内部不再要求 v3/JCS wrapper。 |
| Finish | `finish.json` 中的完整 `FinishDefinition` typed DataStore | `FinishDefinition` 整体 typed DataStore | 与 Payload 独立；内置动作更新必须保留同一 definition 中其它 action；preview 只读取 Payload，Finish 仅在完整 root script 生成时读取。 |
| 导入字体 | `files/fonts/terminal-font.ttf`，`OperationalPreferences`只保存`fonts/terminal-font.ttf`相对引用 | app-private durable asset + typed settings 中的稳定引用 | picker 后复制完整文件再发布选择；替换成功前旧文件仍可用。DataStore 不保存字体 bytes，也不依赖临时 URI。 |
| Payload/Finish 脚本与 LKM | data class 目前保存 `content://` URI，但未取得 persistable grant；freezer 当前明确拒绝这些 kind | app-private durable artifact library，data class 只保存 artifact id | 这是后续真实 consumer 需求；必须 copy-on-import 或持久 grant 二选一。为保证 run 冻结的 exact object，推荐 copy-on-import；未接 resolver 前继续显示 incomplete/fail closed。 |
| backend `.conf`/GLK profile | APK `assets/kernel_profiles/*.conf` 为内置只读；用户 profile repository 尚未落地 | backend-owned profile file library | kernel offset、`kernel_phys_load`、waiter 等继续是文件配置，不塞 Compose/DataStore。内置 asset 不可变；用户导入/生成文件有稳定 id，run 时复制/打开并冻结 exact bytes。设备专属 profile 默认不参加跨设备自动备份。 |
| extractor 输出 | KMP extractor/桌面 JVM 前端尚未落地 | extractor common 返回 typed result；Android/desktop frontend 显式导出 `.conf`/报告 | common 模块不持久化、不知道 Android DataStore。CLI 默认无状态；GUI 自己保存界面偏好，生成 profile 进入上述 profile library 或用户选择路径。 |
| custom backend 包 | 未来 ZIP（manifest、Lua、WebUI、资源），当前未实现 | package directory + package manifest | 包体不进 DataStore；DataStore 只可保存已选 package id/入口偏好。安装、升级、卸载和 WebUI 数据隔离随 custom backend 专项设计。 |
| run input 与 root-script workspace | `noBackupFilesDir/run-input`、app/shell `runhost/<run>`、`files/root-script-preview` | run-scoped ordinary files | 目标是lease/terminal/preview结束即清理且不参与backup；这是目标合同，不是current preview行为。 |
| backend persistent state | `runhost/backend-state/<backendKey>` 每次无条件创建，但当前没有 backend consumer | 暂无通用持久化需求 | 在真实 backend 声明跨 run 状态前不应创建通用目录或协议；未来由该 backend 独占、放在 no-backup 区，禁止借它保存 app 设置。 |
| Run 日志 | `files/run-logs` private GLLOG2 + 可选 MediaStore Downloads | append-only streaming file sink | 单次 run 不设累计上限并支持 10 MiB+；LogHub/`RunReport` 不另建数据库。private 日志的保留/清理是独立 retention policy；已成功发布的 MediaStore 文档归用户，app 不自动回收。 |
| 手动分享日志与诊断包 | `cacheDir/logs`、`cacheDir/diagnostic-*`、`.tar.gz` | cache/share artifact | 可重建、可被系统回收；分享/另存成功不令 cache 成为 authority。FileProvider grant 只覆盖一次分享。 |
| RunHost 状态、LogHub 流、设备/manager 探测 | 内存、binder、`/proc`/系统 API | 不持久化 | run lifecycle、在线状态、SELinux、安装包、root manager 可用性、fd/PID/timestamp 每次重新观察；`RunReport`只作为普通 Info 日志行进入现有 sink。 |
| Compose/SavedStateHandle 页面状态 | `remember`、`rememberSaveable`、`SavedStateHandle` | UI transient state | 只跨重组或系统保存恢复；弹窗、拖拽、滚动、搜索、临时编辑焦点不进入产品 DataStore。真正的用户配置仍由对应 typed repository 保存。 |
| APK/构建期资产 | kernel profile assets、WebView/JNA license assets、Gradle 生成物 | immutable packaged/build artifacts | 不属于运行时持久化中枢；更新随 APK。`build/`、`.so`、测试 fixture 不作为用户数据。 |
| 未来密钥/敏感会话 | 当前无可持久 secret | 默认不持久化；确需长期 secret 时单独使用 Keystore-backed owner | runtime key、SPI、端口、token 用会话帧并用后清零，禁止进入 DataStore、profile、日志或文档。 |

###### DS-D1 exact implementation design（2026-11-08 开发者要求实施）

本轮“实施 DataStore”授权取代本节此前的 `implementation 批批准待申请` 状态，但不放宽先完成 exact design、同岗 critic 与最终树门禁的顺序。实现只包含已有真实 consumer 的五个 aggregate，不建立 `SettingsHub`、反射注册表、第二缓存 authority、全局 mutex、迁移框架或尚无 consumer 的 Diagnose store：

| DataStore 文件 | 直接序列化的完整 data class | owner / consumer |
|---|---|---|
| `ui-preferences.json` | `UiPreferences` | 主进程 application scope；MainActivity/Settings/Home |
| `operational-preferences.json` | `OperationalPreferences` | 主进程 application scope；Settings/Run |
| `app-selection.json` | `AppSelection(selectedExecutor, selectedBackendKey)` | 主进程 application scope；Privilege 与 backend selection 以 `updateData` 分别只改自己的字段 |
| `payload.json` | `PayloadDefinition` | 主进程 application scope；Payload editor/preview/Run freezer |
| `finish.json` | `FinishDefinition` | 主进程 application scope；Finish settings/Run freezer |

`DiagnoseSettings` 仍是 P-DIAG-NATIVE-001/批次02 的已冻结需求，但当前没有 stress UI、Flow snapshot 或 ABI consumer；本批不预建空 store。P-DIAG 实施时必须按 AGENTS §4 新增自己的具名 typed DataStore authority，不能回到 GLK profile，也不能把它塞进上述五个 app aggregate。

1. **库与字节格式**：使用稳定版 `androidx.datastore:datastore:1.3.0` 与现有 `kotlinx.serialization-json`。一个最小 `KotlinxDataStoreSerializer<T>` 只做 immutable data class 的 JSON read/write 与注入的 domain validation；`encodeDefaults=true`、`ignoreUnknownKeys=false`、`explicitNulls=true`。不再写 version wrapper、canonical JCS、key mapper或兼容分支。
2. **旧数据裁定**：不迁移开发期 `settings`、`ghostlock.log/attack/boot/privilege` SharedPreferences，也不导入 `files/definitions/payload-v3.json`/`finish-v3.json`。旧文件可留在安装数据中但没有 production reader/writer；不保留 importer、marker或双写。首次没有新 store 时直接使用 aggregate default。
3. **默认与损坏语义**：UI、Operational、AppSelection 的 malformed store 经 DataStore corruption handler 记录一条 Android error 后原子替换为默认值；Payload/Finish malformed store不装 replacement handler，`read/save/preview/run`传播失败并 fail closed。Payload/Finish 保存前和读取后继续执行现有 identity/kind/shape/size domain validation，不再要求 canonical whitespace/key order。新 store 的 Finish default 精确包含 `finish-restore-selinux`，但显式 `FinishDefinition(items=emptyList())` 仍可用于 Diagnose/preview 的“无 Finish”输入。
4. **进程 owner**：`GhostLockApp` 只在 `Application.getProcessName()==packageName` 时构造 `AppContainer`；`:flowhost` 与 Shizuku host 不构造任何 repository/DataStore。`AppContainer` 自身对错误进程 fail fast，RunHost 继续只消费已冻结 wire/file。
5. **启动边界**：UI、Operational、AppSelection repository 各自只有一个 application-scoped read-only `StateFlow` 投影；`awaitInitial()` 先从真实 DataStore 读取并把该精确值写入投影，再返回。`AppContainer` 只有一个 application-scoped lazy `Deferred<Unit>` 初始化任务，所有 `initialize()` 调用等待同一任务，因此 Activity 重建或某个 caller 取消不会启动第二次读取或留下半初始化状态。该任务完成三个首次读取、恢复 backend stable key并刷新 privilege 后，`MainActivity` 才调用 `setContent`。非 corruption I/O 失败是终态：释放 splash、结束 Activity并记录错误，不用默认值继续，也不无限悬挂。首次非默认主题测试必须观察到 `setContent` 的第一份输入就是落盘值。
6. **顺序与并发**：DataStore actor 是唯一 writer/顺序 owner。Settings/Operational/AppSelection 的命令只提交字段 transform，Payload/Finish 的编辑命令也在当前 authority 上按稳定 identity 做 transform，禁止从 `StateFlow.value` 生成会覆盖其它已提交字段的 stale full snapshot。repository 的 submit 是非阻塞 acceptance 边界并返回 `Deferred`；ViewModel 可立即更新页面 draft，但 committed 投影、跳页或选择成功只在 handle 成功后发布。第二次事件在第一次 save 未完成时仍须被 accepted 并进入同一 actor，测试必须证明第二次 Payload 编辑最终胜出。Payload preview 在 callback 尚未让出主线程时提交当前完整 editor，随后直接使用该 exact transaction handle 返回的 immutable `PayloadDefinition`；不得完成后再 live-read，Finish 始终是显式空值。Run action 同样在 callback 尚未让出主线程时，向 Payload、Finish、Operational 各自的同一 DataStore actor 排入具名 cutoff snapshot transaction/fence；等待并直接使用这三个 fence 返回的 immutable aggregate，再冻结 exact objects、组装 RunHost request。cutoff 后的新编辑排在 fence 后，只进入下一次 run。任一此前已记录失败或 fence 失败都 fail closed。该 fence 只是同一 actor 的线性化读取，不得把普通 `data.first()`叫作barrier，也不得增加第二writer queue、全局mutex、revision或跨store transaction。
7. **字体引用**：Operational aggregate只保存 app-private 相对引用 `fonts/terminal-font.ttf`；picker先完整写入同目录临时文件并原子替换，再提交引用。Run/UI consumer在 `filesDir` 内解析并拒绝绝对路径或 traversal，不持久化 picker URI/绝对 path/font bytes。
8. **备份**：本批在 manifest 显式设置 `android:allowBackup="false"`；不把设备选择、URI、未来 profile 或攻击配置交给 Android cloud/device-transfer。本轮不启用现有未引用 sample backup XML，也不新增一套 include/exclude 规则。
9. **范围边界**：不实现 Diagnose stress、script/LKM artifact resolver、profile library/editor、custom package/WebUI persistence、preview workspace GC或日志 retention；不修改真攻击 backend。Preview workspace GC 的明确 owner 是 `data/run PreviewRootScriptPublisher`，触发器是首次实现重复/连续 preview 生命周期或页面关闭清理；届时同批关闭旧 workspace，不在本批顺手扩展。

逐文件 exact scope 以同批 ledger 的 `P-SETTINGS-IO-001/批次01` 行为唯一权威。验证固定覆盖：五个完整 aggregate round-trip/reopen/default；显示型 corruption→default、Payload/Finish corruption→fail closed；旧 authority production 命中0；主进程/`:flowhost` owner；application初始化幂等、失败有终态且首次非默认主题早于 `setContent`；backend/executor key恢复；两个不同字段并发 transform 不丢失；首写阻塞时接受第二次Payload编辑、preview fence后再接受第三次编辑，当前preview使用第二次而下次使用第三次；save/run交错时run等待已接受提交，run cutoff fence后再接受第三次编辑且当前run仍使用fence值；普通写失败不发布成功、不跳页、不preview/run；Payload-only preview对Finish零读取；manifest backup关闭。Android instrumentation只证明真实DataStore文件、picker和进程边界，不外推为root或真攻击成功。

###### 实施后保留边界

- `GhostLockApp` 已只在精确主进程构造 `AppContainer`；`:flowhost` 与 Shizuku host 不创建或打开上述 DataStore，未引入 multi-process DataStore。
- Payload/LKM/Finish script picker 当前只保存 URI 字符串，既没有 persistable grant，也没有 durable copy；在 artifact library 接通前它们仍不是可跨重启执行的输入。
- `PreviewRootScriptPublisher` 当前每次创建 `files/root-script-preview/<UUID>` 后只返回路径，没有页面结束、下一次preview替换或启动清理；本批只把 `PreviewRootScriptUseCase` 改为 durable Payload-only 读取并对 Finish 零读取。GC 债由 `data/run PreviewRootScriptPublisher` 持有，首次实现重复/连续 preview 生命周期或页面关闭清理时触发偿还；不把“preview结束即清理”写成current事实。
- `runhost/backend-state/<backendKey>` 当前无 consumer；它只是已存在的空 capability，不得被本批解释成已经批准的 backend persistence framework。
- private run log 支持无限累计写入，但当前没有明确 retention UI/策略；本批不以截断单次日志解决磁盘保留问题，retention 是否实现须单独裁定。
- manifest 已显式设置 `android:allowBackup=false`，未启用 sample backup XML。旧 SharedPreferences/旧 Payload/Finish 未迁移，production reader/writer与architecture命中均已归零。

###### 交付跟踪

- 原已完成步骤、批准与运行证据：见 Beads `gl-zgp` notes 的迁移前记录；本设计不维护进度checkbox。
- DS-V1：JVM exact已证明完整data class round-trip/reopen、默认/损坏、幂等初始化、首帧输入、保存/preview/run顺序、owner隔离及旧authority归零；Android test Kotlin源码编译通过，但当前没有获准AVD，instrumentation runtime保持`NOT_RUN`，故本项不提前关闭。



<a id="acceptance-gl-zgp"></a>

### 验收定义与原边界：gl-zgp

<!-- migrated-source: acceptance_criteria gl-zgp -->
NOT_RUN：source与普通门既有完成记录保留；当前没有获准AVD，等待原批准设备/前提后跑exact instrumentation；JVM结果不替代Android，也不擅自换真机。

批准/审核/实际结果：Beads `gl-zgp` notes；本段不维护实时勾选或新PASS。
