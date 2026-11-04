# Kotlin Clean Architecture D0 盘点与 characterization

> 状态：2026-11-04 D7 的标准 `BackendSpec.configurationSpec`、generic adapter/renderer 与 attack owner 迁移已经落到 source，当前 architecture、full unit、Android native/APK 构建与既有 AVD semantic walk 已通过；A301SO 的 mDNS 服务重新出现，但当前 endpoint 拒绝连接，因此 attack 冷机门和依赖它的 D7 最终闭环仍明确未完成。

## 实时进度

- [x] **D7.0 BackendSpec 标准化与残留交织清理设计**：design-critic 对 D7-01..09 逐项复评，最终总判“可进批准 gate”、阻断 0；用户重复命令按计划约定构成 D7 目标批准。BackendSpec outer contract、完整 semantic DTO、authority/失败表、CBN、attack owner/resource/machine disposition 与逐批门已冻结。
- [x] **D7.1 标准 configuration capability 与 architecture 门**：`BackendConfigurationSpec` 已成为 `BackendSpec.configurationSpec` 的 nullable standard capability；UI/domain 不见 `BackendSpec`，data 只见 declaration/captured list，四条 concrete/reverse forbidden edge 当前均为 0。
- [x] **D7.2 generic adapter/semantic UI 收口**：`AndroidBackendConfigurationAdapter` 只按 captured `BackendSpec` 委派 capability；Static/Toggle/ChoiceDocument/Document 只由 generic renderer 渲染。三类 mutation 定点测试及 emulator semantic walk（10 surfaces，其中 4 config cards）通过。
- [~] **D7.3+D7.4 cve43499 attack owner 1:1 迁移**：A301/Mondrian config 已整文件迁入 `backend/backends/cve43499/config/Cve43499DebugConfig.kt`，request factory 已整文件迁入 `backend/backends/cve43499/request/Cve43499RequestFactory.kt`，零 caller 的旧 `FinishExecution.kt` 已删除；参数/算法/顺序冻结。A301SO 当前未连接，真实攻击冷机门未运行，所以本项不置完成。
- [x] **D7.3+D7.4-N current-tree native aggregate 修复**：用户在 design-critic 阻断/重要均归零后明确“批准”；43 项 candidate disposition、33 项 host aggregate、manifest-driven Android artifact/JNI gate 与 XML exact-four-bucket gate 已落地。最终树 `nativeTestHost` 连续两次 33/33，`nativeBuildAndroid` 为 4/AArch64/JNI=6，`checkAll` 为 host 33 + artifact 4 + XML 459/0/0/0，均 exit=0；本项只表示 software gate 修复完成，不替代下一项 A301SO attack gate。
- [~] **D7.5 machine authority/UML/文档闭环**：file-action/component/KSU KEEP rows已按 move/delete 1:1 重生，双语 README、backend policy 与本权威 UML 已同步；2026-11-04 final-tree architecture/full unit/Android native/APK 已复跑通过。当前必须先完成 D7.3+D7.4-N software gate 与 A301SO attack gate，之后才进入 D7.5 最终收口。
- [x] **D0.1 静态 inventory 与五份 machine manifest**：唯一 tool、四个 architecture unit tests、两份 production-composable UI fixture 已落地；当前 TSV 为 463 file-action、74 component files/337 API、88 surfaces。
- [x] **D0.2 原始 D0 基线主机门**：当时的 architecture/full unit、XML 聚合、debug/androidTest/release APK 已串行通过；该历史结果不替代当前最终树的完整回归。
- [ ] **D0.3 Pixel_5 production baseline（按用户强制越权暂停）**：两次 gate 已按 §2 停止并回 debug；`default_boot` 在第二次 emulator 启动未进入 `before-recorded` 前消失，须从 Time Machine 恢复原 snapshot 后再重跑。20 个 M5a 仍保持 `deferred-D5`。
- [ ] **D0.4 A301SO M5 UI walk**：验证 6 个菜单与 3 个 ConfigCard，不 dispatch、不启动真实攻击。
- [ ] **D0.5 pack/final/G1 输入**：生成 deterministic golden tar，运行 final gate，记录五份 manifest SHA 与未验证项。
- [x] **强制越权 D1 纵切**：application-shell settings、device facts、composition root、generated BuildConfig 与 navigation-rail preference 已迁出 presentation concrete repository/util；最小 Kotlin compile 通过。
- [x] **强制越权 D2 component 输入化**：D2a-D2c 已完成；baseline 的 71 个 reusable component 文件未删除，当前含新增语义组件共 74 个，现有 `LocalUiMode` 皮肤分支保留。
- [x] **强制越权 D3a-D3e manager residue 清理**：旧 route/deep-link、manager UI/repository/service/AIDL/globals、Home/Settings 假 manager 状态与未引用 manager strings 已清除；真实 guard、payload、finish/handoff 与 package query KEEP 未改。
- [x] **强制越权 D4a Home 语义纵切**：Home 共用 `HomeUiState/HomeActions` 与单一 `BackendStatusCard` 语义入口；Material/Miuix 只保留外观 renderer。backend catalog 与 privilege 分别经纯 domain port 注入，Shizuku listener/permission/preference 只归 `AndroidPrivilegeAdapter`。
- [x] **强制越权 D4b Backend 页**：路径页共用 `BackendPageUiState/BackendPageActions`；registry/selection/persistence 已收进 catalog/select adapter/use case，`BackendSessionStore.selectedKey` 是 application-scoped 单一选择权威，process-global `BackendSelection` 已删除，两套外观保留。
- [x] **强制越权 D4c-D4e Settings/About/ColorPalette**：三页已使用共享 state/actions（About 仅共享 back action）并由 `LocalUiMode` 选择现有 Material/Miuix renderer；未再读取 manager repository/global。
- [x] **强制越权 D4f Finish/FinishSettings**：FinishSettings 与 payload editor 均已迁到 pure domain port/use case、outer adapter、injected ViewModel 和共享 state/actions；`FinishPage` 只读一次 `LocalUiMode` 并进入独立 `FinishPageMaterial`/`FinishPageMiuix` renderer。页面不再直接读写 `FinishConfig/FinishScriptStore/packageManager`，adapter 保存时保留 handoff-only 字段，真实 payload/guard/handoff 实现未改。
- [x] **强制越权 D5a-D5d Run 执行边界与 renderer seam**：新增纯 `domain/run`、application-scoped `RunCoordinator`、唯一 `BackendRunSessionAdapter`、`RunViewModel/RunUiState/RunActions`；Material/Miuix 生产页面吃同一 state/actions 与共享 terminal rows，保留现有 page-level 外观分支。VM/rotation detach 不 cancel physical run；只有显式 abort 经既有 launcher cancellation 路径传播。
- [~] **强制越权 D5e registry amendment/门禁债**：backend declaration 的 ConfigCard ownership 已移除，presentation-owned renderer 以 semantic config + `LocalUiKit` 渲染；app/FlowHost/Shizuku roots 均捕获一次 registry membership，Flow assembly 改为显式 snapshot 输入，并在 prepare 前对拍 deterministic version/content-hash。source/AIDL compile 已通过；仍待 M5a 20 态 capture、registry mismatch 设备证伪，以及两个 production backend 冷机门。
- [x] **D6 source 收口**：Settings globals、backend config/session、privilege 与 run execution 已迁入 domain port + outer adapter；presentation → data/backend/core、domain → outer、data/backend → UI、非 UI Compose、UI 直连 SharedPreferences/Shizuku 六类边均为 0，并已加入 fail-closed architecture gate。machine ledger、UML 与移动路径指针已收口。
- [x] **D6 当前树 unit/APK/AVD UI 回归**：full unit 448/448、三类 Kotlin source compile、debug APK 构建/部署、Material/Miuix 的 Home/Paths/Run/Settings 明暗主题均已检查；发现并修复 Material 未准备态缺标题及 Home 返回后 readiness 不刷新。该项不替代 Pixel_5 baseline、A301SO 或攻击冷机门。

### D7 当前证据（2026-11-03）

- machine/architecture：`python3 tools/kotlin_architecture_d0.py inventory --repo "$PWD" --out app/src/test/resources/architecture` 与 `python3 tools/kotlin_architecture_d0.py verify --repo "$PWD" --fixtures app/src/test/resources/architecture` 均 exit=0；literal 分别以 `D0_INVENTORY=PASS`、`D0_VERIFY=PASS` 开头，且共同包含 `ARCH_VIOLATIONS=0 UI_IMPLEMENTATION_DEPENDENCY=0 DATA_BACKEND_CONCRETE_IMPLEMENTATION=0 CORE_CONCRETE_BACKEND_IMPLEMENTATION=0 BACKEND_DATA_CVE_REVERSE_DEPENDENCY=0 ... FILE_ACTION_ROWS=463 COMPONENT_FILES=74 COMPONENT_API_ROWS=337`。
- stale source name：对 main/test/androidTest Kotlin/AIDL 执行 exact-word scan；exit=0；literal=`OLD_SOURCE_SYMBOL_HITS=0`，覆盖 `A301SoDebugConfig|FinishHandoffAdapter|FinishExecution|Backend43284ConfigCard|Backend43499GluedProfileCard|CustomLuaConfig`。
- targeted contract：`./gradlew :app:testDebugUnitTest --tests 'com.ghostlock.app.architecture.*' --tests 'com.ghostlock.app.data.backend.AndroidBackendConfigurationAdapterTest' --tests 'com.ghostlock.app.backend.backends.cve43284.BackendCve43284ConfigurationTest' --tests 'com.ghostlock.app.backend.backends.BackendConfigurationCommitOrderingTest' --console=plain`；exit=0；literal=`BUILD SUCCESSFUL in 5s`；XML=`7 files/15 tests/0 failures/0 errors/0 skipped`。
- full unit：`./gradlew :app:testDebugUnitTest --console=plain`；exit=0；literal=`BUILD SUCCESSFUL in 38s`；XML=`76 files/459 tests/0 failures/0 errors/0 skipped`。
- APK/AVD：`./gradlew :app:assembleDebug :app:assembleDebugAndroidTest --console=plain` 已 exit=0、literal=`BUILD SUCCESSFUL`；随后 `adb -s emulator-5554 shell am instrument -w -e suite m5-device-walk -e class com.ghostlock.app.architecture.UiBaselineCaptureTest com.ghostlock.app.test/androidx.test.runner.AndroidJUnitRunner` exit=0，literal=`OK (1 test)`，fixture 断言 10 surfaces/4 configuration cards。
- device gate 当前输入：`adb devices -l`；exit=0；literal 仅列 `emulator-5554 ... device:emu64a`，A301SO serial `QV770MFGJ1` 不在列表。因此已批准的单次 43499 冷机门没有启动，也没有把 package move 当作 exploit PASS。
- native aggregate debt：`make -C core check` exit=2、literal=`make: *** core: No such file or directory. Stop.`；按 `introduction.md` 替代名运行 `./gradlew nativeTestHost nativeBuildAndroid` exit=1、literal=`Task 'nativeTestHost' not found in root project 'GhostLock-App' and its subprojects.`。同一门连续两次失败后已停回 debug；本批 `assembleDebug` 内的 AGP `buildCMakeDebug[arm64-v8a]` 成功只算 Android native build，不替代缺失的 host aggregate task。

### D7 当前树续验（2026-11-04）

- machine/architecture：`python3 tools/kotlin_architecture_d0.py inventory --repo "$PWD" --out app/src/test/resources/architecture`；input=当前 production Kotlin/component/KSU/surface 定义；exit=0；literal=`D0_INVENTORY=PASS ARCH_VIOLATIONS=0 UI_IMPLEMENTATION_DEPENDENCY=0 DATA_BACKEND_CONCRETE_IMPLEMENTATION=0 CORE_CONCRETE_BACKEND_IMPLEMENTATION=0 BACKEND_DATA_CVE_REVERSE_DEPENDENCY=0 ... FILE_ACTION_ROWS=463 COMPONENT_FILES=74 COMPONENT_API_ROWS=337 REMOVE_RESIDUE=0 KEEP_DRIFT=0`。随后 `python3 tools/kotlin_architecture_d0.py verify --repo "$PWD" --fixtures app/src/test/resources/architecture`；input=同批重生 TSV；exit=0；literal=`D0_VERIFY=PASS` 且上述四条 D7 forbidden edge 全为 0。
- stale owner/name：对 `app/src/{main,test,androidTest}` 的 Kotlin/AIDL exact-word scan；input=`A301SoDebugConfig|A301SoDebugException|A301SoDebugParameters|FinishHandoffAdapter|FinishExecution|Backend43284ConfigCard|Backend43499GluedProfileCard|CustomLuaConfig`；exit=0；literal=`OLD_SOURCE_SYMBOL_HITS=0`。
- full unit：`./gradlew :app:testDebugUnitTest --console=plain`；input=当前 final-tree Kotlin/source fixtures；exit=0；literal=`BUILD SUCCESSFUL in 43s`。XML 全量读取 `build/app/test-results/**/testDebugUnitTest/*.xml`；exit=0；literal=`XML_FILES=76 TESTS=459 PASSED=459 FAILURES=0 ERRORS=0 SKIPPED=0 RESULT=PASS`。
- Android native/APK：`./gradlew :app:externalNativeBuildDebug :app:assembleDebug :app:assembleDebugAndroidTest --console=plain`；input=当前 CMake/NDK/Kotlin/androidTest source；exit=0；literal 包含 `:app:configureCMakeDebug[arm64-v8a]`、`:app:buildCMakeDebug[arm64-v8a]`、`:app:externalNativeBuildDebug`、`:app:assembleDebug`、`:app:assembleDebugAndroidTest` 与 `BUILD SUCCESSFUL in 3s`。既存 `ndk.dir` deprecated warning 保留为 build-system debt，不是本批新增告警。
- production identity：debug APK=`/Users/nickji/Documents/Program/Analysis/ghostlock-app/build/app/outputs/apk/debug/GhostLock-v2.0.12(1345)-arm64-v8a-debug.apk`，bytes=`28053501`，SHA-256=`43f10e6659f14879ebc225a3a6805077811fd5e8baa96ef9c007fb1a9025a51b`；其中 `lib/arm64-v8a/lib43499-Glued.so` bytes=`246376`、SHA-256=`0c76ad0b85cf201f83c51ddf6a3096e3b929512b84c81322dee6d61dbbb4b408`、literal=`NOOP_FIXTURE_MARKER=0`。androidTest APK bytes=`1338871`、SHA-256=`fadb732e5766e0e755d7232b2f19dbbb64798660822f0acc9c903edb1609f797`。
- device reachability：`adb devices -l; adb mdns services`；exit=0；literal 包含唯一已连接设备 `emulator-5554 ... device:emu64a`，以及 A301SO 服务 `adb-QV770MFGJ1-XxNlpb _adb-tls-connect._tcp 10.0.152.169:38329`。单次 `adb connect 10.0.152.169:38329`；exit command=0，但 literal=`failed to connect to '10.0.152.169:38329': Connection refused`，随后 device list 仍只有 emulator。因此没有安装上述 APK、没有启动 `Real43499GateTest`、没有产生 exploit 结果或新的 device-gate 归档。
- final diff hygiene：`git diff --check`；input=当前工作树；exit=0；literal=empty。
- 完成判定：D7.1/D7.2 与 D7.3+D7.4 的 source/host/AVD 前置保持绿；D7.3+D7.4 的 A301SO 冷机、固定 CPU 对、单 route、KernelSU 未加载 attack gate 仍是不可替代缺口。另一个独立结构缺口是 `AGENTS.md` §8 仍要求已删除的 `make -C core`，`introduction.md` 又映射到当前未注册的 `nativeTestHost/nativeBuildAndroid`；本轮没有静默降低或改写上位门禁。

### D7.3+D7.4-N current-tree native gate repair（计划指针）

- 唯一权威计划已并入 `docs/plan/kotlin-clean-architecture-ui-plan.md` 的同名小节；本分析文档不再维护第二套 D7.5a/N1-N4 阶段。
- 当前状态：固定 design-critic 三轮复评归零且用户已批准；implementation/software gate 已完成。`verifyNativeTestInventory` positive=`CANDIDATES=43 HOST=33`，缺 avbcheck row mutation 按预期 nonzero；最终树 `nativeTestHost` 普通连续两次均 33/33（`1s`、`979ms`），`nativeBuildAndroid`=`COUNT=4 ABI=arm64-v8a JNI=6`，最终 `checkAll`=`BUILD SUCCESSFUL in 41s` 且 XML=`459/0/0/0`。下一唯一不可替代缺口是 A301SO attack gate。
- software gate 后的单次 `adb devices -l` exit=0，literal 只列 `emulator-5554 ... device:emu64a`；A301SO 未连接，所以本轮没有安装、攻击 dispatch 或新的 `device-gates/` 归档。
- 顺序冻结为：`nativeTestHost/nativeBuildAndroid/checkAll` current-tree software gate → A301SO 单次 cold attack gate → D7.5 final closure；两门均不可由 AVD、局部 CTest 或 AGP compile 单独替代。

## 当前边界

- component：71 个 baseline 文件全部保留；public/internal reusable API 必须逐项登记，不能只数 Composable。
- KSU：管理器 UI/route/repository/service 等进入 REMOVE/RENAME；真实 guard、payload、finish/handoff 与 Manifest package query 进入 KEEP。
- V0.12 后新增的 `BackendStatusCard.kt` KernelSU 来源 KDoc 命中登记为 history rewrite，不进入执行 KEEP，也不删除 component。
- 视觉：Material/Miuix 外观和 `LocalUiMode` 保持；测试只调用 production composable，不复制 renderer。

## 证据

### D0.1 静态 inventory

- 身份：branch=`dev-nocpp`；起始 HEAD=`ffa6497adfc12938133e6338e2f95ddbf2ed54b9`；输入=当前工作树 production Kotlin/component/KSU/surface 定义。
- 命令：`python3 tools/kotlin_architecture_d0.py inventory --repo "$PWD" --out app/src/test/resources/architecture`；exit=0；literal=`D0_INVENTORY=PASS UNCLASSIFIED=0 FILE_ACTION_ROWS=426 COMPONENT_FILES=71 COMPONENT_API_ROWS=357 COMPONENT_UNCLASSIFIED=0 API_KEY_COLLISIONS=0 API_MAPPING_INVALID=0 REMOVE_ROWS=266 KEEP_ROWS=143 REMOVE_RESIDUE=0 KEEP_DRIFT=0 SURFACES=88 D0_CAPTURE=68 DEFERRED_D5=20 SEMANTICS_RECORDS=132 API_KEY_COLLISIONS=0 UNCLASSIFIED=0`。
- 命令：`python3 tools/kotlin_architecture_d0.py verify --repo "$PWD" --fixtures app/src/test/resources/architecture`；exit=0；literal=`D0_VERIFY=PASS UNCLASSIFIED=0 FILE_ACTION_ROWS=426 COMPONENT_FILES=71 COMPONENT_API_ROWS=357 COMPONENT_UNCLASSIFIED=0 API_KEY_COLLISIONS=0 API_MAPPING_INVALID=0 REMOVE_ROWS=266 KEEP_ROWS=143 REMOVE_RESIDUE=0 KEEP_DRIFT=0 SURFACES=88 D0_CAPTURE=68 DEFERRED_D5=20 SEMANTICS_RECORDS=132`。
- Android fixture 首次 compile 暴露并修复 3 个 test API/Composable 调用错误；最终 `:app:assembleDebugAndroidTest` 通过，未保留该失败状态。

### D0.2 主机门

- architecture：`./gradlew :app:testDebugUnitTest --tests 'com.ghostlock.app.architecture.*' --console=plain`；输入=五份 TSV；exit=0；literal=`BUILD SUCCESSFUL in 1s`。
- full unit：`./gradlew :app:testDebugUnitTest --console=plain`；exit=0；literal=`BUILD SUCCESSFUL in 35s`。XML 全量聚合：`XML_FILES=72 TESTS=442 PASSED=442 FAILED=0 FAILURES=0 ERRORS=0 SKIPPED=0`；exit=0。
- build：`:app:assembleDebug`、`:app:assembleDebugAndroidTest`、`:app:assembleRelease` 均 exit=0；literal 分别为 `BUILD SUCCESSFUL in 2s`、`BUILD SUCCESSFUL in 2s`、`BUILD SUCCESSFUL in 38s`。Release 输出只有既存 production warning；D0 新增 androidTest 最终编译无新增 warning。
- DEBUG_APK=`/Users/nickji/Documents/Program/Analysis/ghostlock-app/build/app/outputs/apk/debug/GhostLock-v2.0.12(1345)-arm64-v8a-debug.apk`；bytes=28420612；SHA-256=`325b75a8e3d5c5d94f0d39a78e8a9c16116007ddf7ecb300487a3cc14245b87f`。
- TEST_APK=`/Users/nickji/Documents/Program/Analysis/ghostlock-app/build/app/outputs/apk/androidTest/debug/GhostLock-v2.0.12(1345)-debug-androidTest.apk`；bytes=1297913；SHA-256=`c5f5250d628ae2852492da8ea91361a3e9fac10f4cc3ad336333b88c44fcff44`。
- RELEASE_APK=`/Users/nickji/Documents/Program/Analysis/ghostlock-app/build/app/outputs/apk/release/GhostLock-v2.0.12(1345)-arm64-v8a-release.apk`；bytes=3534079；SHA-256=`4611aa2879ab05acf97da619750cf7ae180dcb1afbfb010318aceee5153c7cbe`。

### D0.3 第一次 gate / debug 结果

- 独占清理：原 `Pixel_5` 为 PID 38508 / `emulator-5582`；命令=`adb -s emulator-5582 emu kill`；exit=0；literal=`PIXEL_5_CONFLICT_CLEARED=1 PID=38508 SERIAL=emulator-5582`。
- attempt 1：capture 在进程筛选中把自身 `--avd Pixel_5` 误判为 emulator；exit=2；literal=`D0_CAPTURE=FAIL FIELD=avd-exclusive ACTUAL=1`。已定点改为只匹配 `qemu-system-*`/emulator 进程。
- attempt 2：启动前五项 static identity 全部匹配；运行约 189 秒后，transaction 仍为 `stage=spawned,before=null`，cleanup 读取 `/Users/nickji/.android/avd/Pixel_5.avd/snapshots/default_boot` 得 `FileNotFoundError`；进程与 `emulator-5580` 已消失，但 `snapshots/` 为空。由于原实现把 emulator stdout 丢弃，不能把“snapshot 为何消失”写成已证实因果。
- debug 修复：emulator stdout 改写 `build/reports/kotlin-architecture-d0/emulator.log`；preflight/finally/recover 对 snapshot 缺失 fail-closed；serial 消失增加有界等待；旧 transaction 现为 `STATE_STATUS=recovery-failed STAGE=recovery-failed RESTORE_ERRORS=['static-identity-missing:FileNotFoundError']`。
- 恢复来源检查：`tmutil isexcluded /Users/nickji/.android/avd/Pixel_5.avd/snapshots` literal=`[Included]`；存在 2026-11-02 local Time Machine snapshot，但当前 Codex shell 读取备份卷/挂载 APFS snapshot 均被 macOS Full Disk Access/root 拒绝。
- 下一动作：从 Time Machine 只恢复 `/Users/nickji/.android/avd/Pixel_5.avd/snapshots/default_boot`；恢复后 tool 会先重验 `FILES=5 BYTES=3063024640 MANIFEST_SHA=9a474d743d26fee0a1d5c9846cb887c5980d1a27f3302131268ecdaf94e26c02`，匹配才允许第三次 gate。

D0.3-D0.5 尚未完成，未预填 PASS。

## 2026-11-03 强制越权：直接进入 settings 纵切

> 用户连续指令“跳过验证给我直接进入整理”与“暂时别管critic design和批准了，直接推进”，明确越过 D0.3、G1、design-critic 与逐批批准。本段登记沿革和技术债，不把越权写成门禁已通过。

- [x] 新增纯 Kotlin `domain/settings`：`UiPreferences`、`UiPreferencesPort`、`ObserveUiPreferencesUseCase`。
- [x] 新增 `data/settings/SharedPreferencesUiPreferencesRepository`，保留既有 `settings` 文件名、key 与默认值；Android listener 归 data adapter 所有。
- [x] 新增 `AppContainer` 与 `AppViewModelFactory`，由 `GhostLockApp` 启动期构造 adapter/use case，`MainActivity` 不再隐式构造 settings repository。
- [x] `MainActivityViewModel` 已从 `ui.data.repository.SettingsRepository*`、`ksuApp` 和 Android `SharedPreferences` listener 解耦，改为只依 domain use case。
- [x] `ThemeController` 改为把 `UiPreferences` 映射成现有 `AppSettings`，不再读取具体 repository。
- [x] 新增纯 Kotlin `domain/device` 与 Android `data/device` adapter；`HomeViewModel` 不再读取旧 `ui/Kernels.kt`/`ui/util/SELinuxChecker.kt`，两个旧文件已删除。
- [x] 删除手写 `ui/BuildConfig.kt`；production caller 统一读取 AGP generated `com.ghostlock.app.BuildConfig.IS_PR_BUILD`。
- [x] navigation rail 改为 `UiPreferences` + domain command 输入；Material/Miuix rail 不再在 Composable 内构造 `SettingsRepositoryImpl`。
- [x] D2a bottom chrome 输入化：Material/Miuix bottom bar 与 rail 统一接收 neutral `MainNavigationDestination`、page count、`fullFeatured` 与按 page-index 的 badge；五个 component 文件不再读取 `Natives`，历史 `Module/SuperUser` enum 字段已改为 `Backend/Run`。
- [x] D2b profile component 输入化：新增 `ProfileDraft/ProfileFlag/ProfileTemplateOption`，`Capabilities/Groups` 移入 reusable component package；七个 profile component 不再引用 `Natives.Profile`、`KsuCli`、template ViewModel 或 template/sepolicy 存储 API，catalog/选择/默认值/validator 均由 caller 注入；Material/Miuix 继续由 `LocalUiMode` 分支渲染。
- [x] D2c capability inputization：markdown 改用 caller Android context asset；send-log 注入 report command 并中性化文件名；`KsuValidCheck` 重命名为 `CapabilityGate`；reboot availability/options/action 全部由 caller 注入；旧 root/KernelSU bugreport collector 删除，改为 `domain/diagnostics` port/use case + `data/diagnostics` 非特权 GhostLock report adapter。
- [x] D3a route reachability：旧 manager route/deep-link/ZIP action 与 dispatcher 已删除。
- [x] D3b manager cluster：manager screens、repository、service、AIDL、`KsuApp/KsuService/Natives/KsuCli` 已删除；component 与真实 handoff KEEP 未删除。
- [x] D3c Home：旧 manager 安装/root/LKM 状态已由 device/backend/update domain facts 取代；Home renderer 共享状态与动作。
- [x] D3d Settings：假 SU/module/SU-log/umount/ADB-root controls 与 concrete repository 已删除；只保留实际 app shell/color preferences。
- [x] D3e resources：theme/learn branding 已 GhostLock 化；未引用 module/SU-log/settings manager 资源共移除 222 项，旧 module-description preference/UI 卡同步清零；真实 finish/root-manager handoff 资源保留。
- [x] backend catalog 边界：新增纯 domain `BackendRuntimeSpec/BackendRegistrySnapshot/BackendCatalogPort/GetBackendCatalogUseCase` 与 data adapters；`HomeViewModel` 不再 import `Backends/BackendSelection` 或 UI tone。
- [x] Backend 页边界：新增 `BackendSelectionPort/SelectBackendUseCase/BackendSelectionAdapter` 与 `BackendConfigRenderer`；`PathViewModel` 降为普通 ViewModel，页面/VM 均不 import `Backends/BackendSelection/BackendSpec`，AppContainer 只复制一次 registry membership。
- [x] Backend 页最小 compile：`./gradlew :app:compileDebugKotlin --console=plain`；exit=0；literal=`BUILD SUCCESSFUL in 1s`。androidTest fixture 已按新 state/actions/renderer 参数更新，但按用户指令未运行 androidTest/AVD。
- [x] FinishSettings 子批：新增 `FinishSettingsPort`、read/save use cases、adapter 与 `FinishSettingsViewModel`；save 只 copy `restoreSelinux/softRebootAfterFinish`，明确保留 payload/manager/safe-mode/relay 字段；页面与摘要卡共用 `FinishSettingsUiState/Actions`。最小 compile exit=0、literal=`BUILD SUCCESSFUL in 839ms`。
- [x] Finish payload editor 子批：新增 `FinishEditorState/Port` 与 read/save/preview use cases、`FinishEditorAdapter`、`FinishEditorViewModel`；payload/root-manager/safe-mode/order/edit/dialog 全部经共享 state/actions，adapter 基于当前 `FinishState.copy(...)` 只替换 editor-owned 字段并保留 SELinux/relay/soft-reboot 等 handoff-only 字段。页面 direct boundary scan empty；最小 compile exit=0、literal=`BUILD SUCCESSFUL in 1s`。
- [x] Run D5a-D5d 子批：旧 731 行两套私有状态/dispatch 已收敛为 501 行 page-level renderer；新增 `RunCommand/RunSessionState/RunSessionPort/RunBackendUseCase`、application-owned `RunCoordinator`、outer `BackendRunSessionAdapter`、`RunViewModel/RunUiState/RunActions`，并删除无 caller 的旧 `BackendRunViewModel`。Run domain forbidden-import scan empty，Run presentation 对 dispatcher/gate/registry/core/Android Intent/Context direct scan empty，`git diff --check` exit=0。
- [x] D5e 当前实现子批：`BackendSpec` 的 Composable/`ConfigCard()` ownership 已删除；presentation renderer boundary 只传 stable key 并从 `LocalUiKit` 取组件皮肤。`FlowEntryRegistryAssembly` 不再读取 `Backends.all`，由 App/FlowHost/Shizuku composition root 的 immutable list 显式输入。最小 `./gradlew :app:compileDebugKotlin --console=plain` exit=0、literal=`BUILD SUCCESSFUL in 1s`；只证明 Kotlin/source 合成，未运行 androidTest、AVD 或攻击冷机门。
- [x] D5e registry identity 子批：新增 deterministic length-prefixed membership canonicalization（order/key/selectable/flowSymbol）与 64-bit version + SHA-256；app identity 经 run adapter/dispatcher/parent 传入 launcher，`:flowhost`/Shizuku AIDL 分别导出 host identity，launcher 在 prepare 前 mismatch fail-closed。`./gradlew :app:compileDebugKotlin --console=plain` exit=0、literal=`BUILD SUCCESSFUL in 2s`；AIDL 10/11 与 19/20 已生成编译，但设备 mismatch/一致性分支未跑。
- [x] D6 operational settings 子批：新增纯 `OperationalPreferences/Port/UseCases` 与 `SharedPreferencesOperationalPreferencesAdapter`；后者直接成为既有三个 store 的唯一 mutable authority，旧 `AttackSettings/BootSettings/LogSettings` singleton 已删除。Material/Miuix renderer 仅消费同一 `SettingsUiState/SettingsScreenActions`；`BackendRunSessionAdapter` 从同一 domain stream 读取 rerun/timestamp/font facts。presentation forbidden-import、domain forbidden-import、data reverse-import 与 `ui/data/**` 四项扫描均为 0；最终最小 compile exit=0、literal=`BUILD SUCCESSFUL in 938ms`。
- [x] D6 config/session/privilege 子批：backend config 恢复与 typed store 移出 UI；`BackendConfigurationPort` 向 presentation 只暴露 `Static/Toggle/ChoiceDocument/Document` 语义状态，具体 backend/profile/Lua/URI 处理只在 `AndroidBackendConfigurationAdapter`。`BackendSessionStore` 实例同时服务 catalog/select/run，旧 `BackendSelection` global 删除。Home 与 Run 共用 `PrivilegeState`，UI 不再注册 Shizuku listener 或读取 executor preference。
- [x] D6 renderer 收口子批：About/Home/Backend/Run/Settings/ColorPalette/Finish/FinishSettings 均在页面根绑定共享 state/actions，再由 `LocalUiMode` 选 Material/Miuix renderer；Finish 的页面级 Boolean router 已拆成两个具名 renderer，辅助 payload 组件仍消费同一语义 model。
- [x] D6 machine ledger/architecture gate 子批：`kotlin_architecture_d0.py` 已能在删除 closure 落地后记录 `ABSENT`、避免 delivered target 重复、按当前 component public surface 重建并排除局部变量误报；新增 package-edge gate fail-closed 检查 domain/data/backend/ui/Compose/SharedPreferences/Shizuku 六类违规。当前 inventory exit=0、literal=`D0_INVENTORY=PASS ARCH_VIOLATIONS=0 UNCLASSIFIED=0 FILE_ACTION_ROWS=457 COMPONENT_FILES=74 COMPONENT_API_ROWS=336 COMPONENT_UNCLASSIFIED=0 API_KEY_COLLISIONS=0 API_MAPPING_INVALID=0 REMOVE_ROWS=70 KEEP_ROWS=168 REMOVE_RESIDUE=0 KEEP_DRIFT=0 SURFACES=88 D0_CAPTURE=68 DEFERRED_D5=20 SEMANTICS_RECORDS=132 API_KEY_COLLISIONS=0 UNCLASSIFIED=0`；verify exit=0、literal=`D0_VERIFY=PASS ARCH_VIOLATIONS=0 UNCLASSIFIED=0 FILE_ACTION_ROWS=457 COMPONENT_FILES=74 COMPONENT_API_ROWS=336 COMPONENT_UNCLASSIFIED=0 API_KEY_COLLISIONS=0 API_MAPPING_INVALID=0 REMOVE_ROWS=70 KEEP_ROWS=168 REMOVE_RESIDUE=0 KEEP_DRIFT=0 SURFACES=88 D0_CAPTURE=68 DEFERRED_D5=20 SEMANTICS_RECORDS=132`。
- [x] 当前 source compile：`./gradlew :app:compileDebugKotlin :app:compileDebugUnitTestKotlin :app:compileDebugAndroidTestKotlin --console=plain`；最终 exit=0；literal=`BUILD SUCCESSFUL in 1s`。这只证明 main/unit-test/androidTest Kotlin source 合成，不替代 test execution、APK、AVD 或真机门。
- [x] 当前定点 contract unit：architecture edge gate、session authority/legacy resolve、dynamic registry、flow selection、uid gate、privilege state/use cases 共 7 个 XML/30 tests；两次定点命令均 exit=0、literal=`BUILD SUCCESSFUL in 1s`，合计 `TESTS=30 FAILED=0 ERRORS=0 SKIPPED=0`。首次 Robolectric fixture 因当前 JRE module export 失败，已改为对纯 `BackendSessionSelection` contract 测试并复跑通过，未把环境失败算作产品结论。
- [x] D6 历史指针终检：README、当前 UML/analysis、production/test/androidTest 中旧 `ui/backend/{BackendConfig,BackendFlowParams,BackendParameterSchema,BackendReadiness,BackendRunGate,BackendRunUiState,BackendSessionStore,FlowLaunchParent}` 路径引用为 0；已删除 manager import（`KsuApp/KsuService/Natives/Kernels/ui.data/KsuCli/IKsuInterface`）引用为 0；`git diff --check` exit=0。历史替代图与 machine ledger 的 `ABSENT` 行继续保留为沿革证据。
- [x] 当前最小 compile：`./gradlew :app:compileDebugKotlin --console=plain`；首轮因已删 preference key 仍留在 `OBSERVED_KEYS` 而 exit=1，定点移除后复跑 exit=0、literal=`BUILD SUCCESSFUL in 2s`。完整 test/AVD/critic 仍按用户指令跳过。
- [x] 最小 compile 门：settings/device/rail command 纵切为 `BUILD SUCCESSFUL in 1s`；D2a 首次因 rounded icon alias receiver 写错而 exit=1，定点改为 `Icons.Rounded.<alias>` 后同命令 exit=0、literal=`BUILD SUCCESSFUL in 7s`。D0 AVD/golden、critic、G1 和完整验证按用户指令暂不执行。
- [x] 当前 full unit/source gate：`./gradlew :app:compileDebugKotlin :app:compileDebugUnitTestKotlin :app:compileDebugAndroidTestKotlin :app:testDebugUnitTest --console=plain`；exit=0；literal=`BUILD SUCCESSFUL in 36s`。XML 全量聚合=`XML_FILES=73 TESTS=448 PASSED=448 FAILED=0 FAILURES=0 ERRORS=0 SKIPPED=0`；`git diff --check` exit=0。
- [x] 当前 machine gate：inventory/verify 均 exit=0；literal 分别为 `D0_INVENTORY=PASS ARCH_VIOLATIONS=0 UNCLASSIFIED=0 FILE_ACTION_ROWS=457 COMPONENT_FILES=74 COMPONENT_API_ROWS=336 COMPONENT_UNCLASSIFIED=0 API_KEY_COLLISIONS=0 API_MAPPING_INVALID=0 REMOVE_ROWS=70 KEEP_ROWS=168 REMOVE_RESIDUE=0 KEEP_DRIFT=0 SURFACES=88 D0_CAPTURE=68 DEFERRED_D5=20 SEMANTICS_RECORDS=132 API_KEY_COLLISIONS=0 UNCLASSIFIED=0` 与 `D0_VERIFY=PASS ARCH_VIOLATIONS=0 UNCLASSIFIED=0 FILE_ACTION_ROWS=457 COMPONENT_FILES=74 COMPONENT_API_ROWS=336 COMPONENT_UNCLASSIFIED=0 API_KEY_COLLISIONS=0 API_MAPPING_INVALID=0 REMOVE_ROWS=70 KEEP_ROWS=168 REMOVE_RESIDUE=0 KEEP_DRIFT=0 SURFACES=88 D0_CAPTURE=68 DEFERRED_D5=20 SEMANTICS_RECORDS=132`。
- [x] 当前 AVD 外观回归：`GhostLock-v2.0.12(1345)-arm64-v8a-debug.apk` 已部署到 `emulator-5554`（`Google sdk_gphone64_arm64`，kernel `6.1.23-android14-4-00257-g7e35917775b8-ab9964412`）；Material/Miuix 各自检查 Home/Paths/Run/Settings，Material 的 UI-style/profile 下拉框另在明暗主题打开检查。截图与 UI XML 位于 `/Users/nickji/Documents/Program/Analysis/ghostlock-app/build/reports/kotlin-clean-avd/`；未 dispatch 攻击。
- [x] Material 未准备态修复：`BackendStatusCardMaterial` 现在固定以 `Not ready` 为标题、backend 短句为原因，并把 `Configure Paths` 与 CVE badge 放在底部同行；当前 AVD literal=`Not ready | Select a valid built-in or imported profile before running | Configure Paths | CVE-2026-43499-Glued`。Miuix renderer 保持大标题、特色卡片、大背景 icon 与底部导航/badge 布局。
- [x] Home readiness 刷新修复：AVD 暴露“Paths 清空 profile 后 Home 仍显示 Ready”；根因是 `HomePager` 仅首次激活刷新。现改为每次 `isCurrentPage` 变为 true 都调用 `HomeViewModel.refresh()`。同一安装内实测 `Select built-in → Home=Ready/Open Run → Clear profile → Home=Not ready/Configure Paths`，末态恢复为 Miuix + light + 未选择 profile。
- [ ] 技术债：补 backend config mutation adapter characterization、settings adapter key/default characterization、Pixel_5 D0 historical visual baseline、registry mismatch 设备证伪与攻击冷机门；未偿还前不得把当前 UI/source 回归写成完整攻击 runtime 回归。

越权代价：D1-D6 source slices 被合为连续纵切；当前 debug APK/AVD UI 与 full unit 已补，但 Pixel_5 历史 baseline、registry identity 设备证伪、A301SO 与攻击冷机门仍未补。下一活动 checkbox 为 backend config mutation adapter characterization；M5a/设备门继续登记未执行。
