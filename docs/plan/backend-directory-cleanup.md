# backend 目录整理（M1–M5：package/目录对齐 + core 拆分 + backends 统一 + @UnfinishedBackend）

> 状态：**M1–M5 代码侧已交付（2026-11-02 / 2026-11-03；交付记录见 §9、§12）**；设备回归由 Lead 执行并归档。
> 范围：**纯结构搬迁/包名对齐 + 未完工注解，不改任何运行行为**；M3–M5 用户裁定：不删除不可选/未完工代码（§7、§11）。

## 1 现状乱点（实测）
- 10 个文件 package 与目录不一致（`core/`、`lua/`、`cve43284/`、`cve43499/`、`ui/screen/module/backends/`）。
- `backend/core/` 11 文件混杂：契约（BackendSpec）、注册表（Backends）、host（Service/Protocol/Params/Path/Exec）、launcher×2、debug fixture、UI 卡 helper。
- `backend/declarations/` 名义存在但只有 1 文件，声明包文件散落各 backend 目录。
- `backend/general/` 空目录。
- UI 文件（FinishUi/FinishSettingsUi/CustomLuaConfig/DefaultBackendConfigCard）错放 backend 包。

## 2 目标结构
```
backend/
  declarations/   BackendSpec、默认/自定义卡?、旧原版声明、Profiles43499、Lua 声明、FlowEntryRegistryAssembly
  registry/       Backends（唯一注册表）
  host/           FlowEntryParams、FlowHostProtocol、FlowHostService、HostProcessExec、HostRunPaths
  launcher/       AppProcessFlowLauncher、ShizukuProcessFlowLauncher
  diagnostics/    DiagnosticFlowEntries（debug 由 BuildConfig 门控；移到真正 debug source set 需 DI，另批）
  cve43499/       旧攻击实现（M3/M4 决策；本批不动）
  cve43499glued/  当前 43499-Glued
  cve43284/       旧原版实现（M3/M4）
  cve43284glued/  当前 43284-Glued
  debugprobe/     debug probe
  lua/            其余 Lua 运行代码
ui/component/backend/  DefaultBackendConfigCard、CustomLuaConfig（UI 归 UI）
```
- 所有文件 package 与目录一致（`ui/screen/module/backends/Finish*` 改回 `ui.screen.module.backends` 等）。
- 删除 `backend/general/` 空目录。

## 3 迁移映射（逐文件）
| 旧路径 | 新路径 | 新 package |
|---|---|---|
| backend/core/BackendSpec.kt | backend/declarations/BackendSpec.kt | 不变 declarations |
| backend/core/Backends.kt | backend/registry/Backends.kt | backend.registry |
| backend/core/AppProcessFlowLauncher.kt | backend/launcher/AppProcessFlowLauncher.kt | backend.launcher |
| backend/core/ShizukuProcessFlowLauncher.kt | backend/launcher/ShizukuProcessFlowLauncher.kt | backend.launcher |
| backend/core/FlowEntryParams.kt / FlowHostProtocol.kt / FlowHostService.kt / HostProcessExec.kt / HostRunPaths.kt | backend/host/… | backend.host |
| backend/core/DiagnosticFlowEntries.kt | backend/diagnostics/DiagnosticFlowEntries.kt | backend.diagnostics |
| backend/core/DefaultBackendConfigCard.kt | ui/component/backend/DefaultBackendConfigCard.kt | ui.component.backend |
| backend/lua/BackendCustom.kt | backend/declarations/BackendCustom.kt | 不变 declarations |
| backend/lua/CustomLuaConfig.kt | ui/component/backend/CustomLuaConfig.kt | ui.component.backend |
| backend/cve43499/BackendCve202643499.kt / Profiles43499.kt | backend/declarations/… | 不变 declarations |
| backend/cve43284/BackendCve202643284.kt | backend/declarations/… | 不变 declarations |
| ui/screen/module/backends/FinishUi.kt / FinishSettingsUi.kt | 原地，改 package | ui.screen.module.backends |
| backend/cve43499/flow/Cve43499CleanFailureReason.kt | 原地，改 package | backend.cve43499.flow |
- 同步更新 main/test/androidTest 全量 imports；`com.ghostlock.app.backend.declarations.Finish*` 引用改为 ui 包。

## 4 门禁
- 脚本：package vs 目录 mismatch = 0（main/test/androidTest）。
- `:app:compileDebugKotlin`；全量 `:app:testDebugUnitTest`；`:app:assembleDebug :app:assembleDebugAndroidTest :app:assembleRelease`；`git diff --check`。
- 设备 A301SO：debug UI 菜单/各 ConfigCard 走查、`DebugProbeHostSmokeTest`、B2 smoke；release 菜单仍无 probe。
- 备份：`/tmp/ghostlock-backend-dir-cleanup-backup-<UTC>/` + MANIFEST.sha256；不 commit。

## 5 不做/后续
- 不删旧 cve43499 47 文件/12k 行（M4 需用户裁定）；不做 active/legacy 目录分区（M3）。
- DiagnosticFlowEntries 未移到 `app/src/debug`（需 debug/main 桥接设计），本批只归入 `backend/diagnostics` 且保持 BuildConfig 门控。


## 6 交付记录（2026-11-02）

- MOVED 16：`backend/core/` 拆分 → `backend/host/`（5）、`backend/launcher/`（2）、`backend/registry/Backends.kt`、`backend/diagnostics/DiagnosticFlowEntries.kt`、`backend/declarations/`（BackendSpec + 原版 43499/43284 声明、Profiles43499、Lua BackendCustom）、`ui/component/backend/`（DefaultBackendConfigCard、CustomLuaConfig）；删除空目录 `backend/core/`、`backend/general/`。
- package 修正 3：`ui/screen/module/backends/FinishUi.kt`/`FinishSettingsUi.kt` → `ui.screen.module.backends`；`backend/cve43499/flow/Cve43499CleanFailureReason.kt` → `...cve43499.flow`。
- `AndroidManifest.xml` FlowHostService FQN → `backend.host.FlowHostService`；main/test/androidTest 全量 import/FQN 更新；逻辑/签名/文案零改动（备份 vs 当前 diff：49 文件有变、0 条 unexpected 行）。
- 门禁：package/dir mismatch **CHECKED=449 MISMATCH=0**；全量单测 **434/0/0/0**；`compileDebugKotlin`、`assembleDebug/AndroidTest/Release` 全 PASS；`git diff --check` EXIT=0。
- 设备 A301SO：debug UI 菜单 6 项/卡片正常；release 菜单 5 项（无调试探针）；`DebugProbeHostSmokeTest` OK(2)、`FlowHostProcessSmokeTest` OK(4)、`ShizukuFlowHostSmokeTest` OK(4)（FlowHostService 新 FQN bind smoke 通过）。
- 备份：`/tmp/ghostlock-backend-dir-cleanup-backup-20261103T000433Z`（52 文件，MANIFEST `059e9c3d…`）。
- 遗留：测试源码目录仍为 `backend/core`（package 一致，mismatch=0；随 M3/M4 迁移）；空目录 `backend/cve43499/debug/`；M3 active/legacy 分区与 M4 旧代码删除待用户裁定。


## 7 M3–M4：active / unfinished 分区（2026-11-02 用户裁定）

> 用户原话：「做m3/m4，不必删除不可选代码，那是未完工的而不是弃用的」。
> 结论：**不删除任何不可选 backend 生产代码**；把它们从根级移入 `backend/unfinished/`，语义=未完工（保留注册/灰显/配置卡），不是弃用。

### 7.1 目标结构（补充 §2）
```
backend/
  declarations/   BackendSpec/BackendRow/BackendSelection、FlowEntryRegistryAssembly（框架契约）
  host/ launcher/ registry/ diagnostics/（不变）
  active/
    cve43499glued/   package com.ghostlock.app.backend.active.cve43499glued
    cve43284glued/   package ...active.cve43284glued
    debugprobe/      package ...active.debugprobe
  unfinished/
    cve43499/        package ...unfinished.cve43499（含 victim/flow/generator/... 子包）
      BackendCve202643499.kt、Profiles43499.kt（从 declarations 移入）
    cve43284/        package ...unfinished.cve43284
      BackendCve202643284.kt（从 declarations 移入）
    lua/             package ...unfinished.lua
      BackendCustom.kt（从 declarations 移入）+ 其余 Lua 运行代码
```
- `backend/README.md`：说明 active=可运行（含 debug-only probe）、unfinished=未完工但保留（不可选/灰显/保留配置卡与实现），禁止以“清理”为名删除 unfinished。
- 测试源码镜像迁移：`app/src/test/kotlin/.../backend/{cve43499,cve43284,lua}` → `.../backend/unfinished/...`；active 对应测试同理；androidTest 中 `cve43499/victim` 等随包迁移。
- manifest/AIDL 不变（host 包未动）。
- 不删除任何生产代码；仅移动/改 package/import；空目录可删。

### 7.2 M4 收口
- package/dir mismatch=0；旧包前缀（`backend.cve43499`/`backend.cve43284`/`backend.lua`/`backend.cve43499glued`/`backend.cve43284glued`/`backend.debugprobe`）在 app/src 引用=0。
- 文档：README、UML §11.9.1 布局注记、`backend-directory-cleanup`/评审日志；计划状态更新。
- 门禁：compile/全量单测/debug+androidTest+release 构建/`git diff --check`；设备 debug 菜单/卡片、release 菜单、probe/B2/Shizuku 回归。

## 8 范围之外（明确不做）
- 不删除/归档 unfinished 实现（用户明确：未完工非弃用）。
- 不改攻击逻辑/profile/flow 契约；不改 manifest/AIDL。
- 旧 cve43499（47 文件）保留原实现，只做物理/包名归类。


## 9 M3–M4 交付记录（2026-11-02 · 代码侧）

> 用户原话：「做m3/m4，不必删除不可选代码，那是未完工的而不是弃用的」。
> 结论：全部不可选 backend 仅物理/包名归类到 `backend/unfinished/`，**未删除任何生产/测试文件**。

- **MOVED 105 文件 / 11 个目录整体移动**（`backend/active/` + `backend/unfinished/`）：
  - active：`cve43499glued`（5）→ `.../active/cve43499glued`；`cve43284glued`（3）→ `.../active/cve43284glued`；`debugprobe`（2）→ `.../active/debugprobe`；package 前缀 `com.ghostlock.app.backend.active.`。
  - unfinished：`backend/cve43499/**`（45）+ `declarations/BackendCve202643499.kt`、`declarations/Profiles43499.kt` → `backend/unfinished/cve43499/`（47 文件；victim/flow/generator/lifecycle/owned/perf/port/w3/finish 等子包整体移入；package 前缀 `...backend.unfinished.cve43499`）。
  - unfinished：`backend/cve43284/**`（2）+ `declarations/BackendCve202643284.kt` → `backend/unfinished/cve43284/`（3 文件；package `...unfinished.cve43284`）。
  - unfinished：`backend/lua/**`（4）+ `declarations/BackendCustom.kt` → `backend/unfinished/lua/`（5 文件；package `...unfinished.lua`）。
  - 测试镜像：test 33 文件（`unfinished/cve43499` 30、`active/cve43499glued` 2、`active/cve43284glued` 1）、androidTest 7 文件（`unfinished/cve43499` 6、`active/cve43284glued` 1）随生产同步迁包；其它引用旧包的测试只改 package/import/FQN。
- `backend/declarations/` 仅保留框架契约：`BackendSpec.kt`（BackendSpec/BackendRow/BackendSelection）、`FlowEntryRegistryAssembly.kt`；4 个 backend 声明移出并在新包内补 `BackendSpec`/`BackendRow` import。
- 新增 `backend/README.md`：active（可运行，debug probe 仅 debug）/unfinished（**未完工、保留、不可选/灰显、配置卡保留、禁止以清理为名删除**）政策 + 目标结构 + 包名/测试镜像规则。
- 引用同步：main/test/androidTest 全量 package/import/FQN 重写（含 `FlowSymbol` 字符串、`VictimArchitectureTest` 禁止 token、Kdoc 源码路径）；行为/签名/文案/manifest/AIDL/攻击逻辑零改动。
- **内容零删改证明**：备份 127 文件（105 移动 + 20 原地改 + 2 文档）；MANIFEST 127/127 `shasum -c` OK；对每个 app/src 文件做机械替换逆变换回原件 **125/125 IDENTITY**（差异仅 package/import/FQN 行）；移动前后 main/test/androidTest backend 的 `.kt` 计数 76/38/17 均不变。
- **门禁（2026-11-02 本机）**：package/dir mismatch **CHECKED=449 MISMATCH=0**；旧包前缀在 `app/src` **=0**；`:app:compileDebugKotlin` **BUILD SUCCESSFUL**；全量 `:app:testDebugUnitTest` **EXIT=0 / 434 tests, 0 failures, 0 errors, 0 skipped（67 XML）**；`:app:assembleDebug :app:assembleDebugAndroidTest :app:assembleRelease` **BUILD SUCCESSFUL**（三 APK 已产出）；`git diff --check` **EXIT=0**。
- 备份：`/tmp/ghostlock-backend-m3m4-backup-20261103T001839Z`（127 文件，MANIFEST sha256 `ff298635c98084ca67a995aed93bddb7b11b9076c517744ed364ee28de314ecc`）；未 commit。
- **UNVERIFIED（待 Lead 设备回归）**：A301SO debug 菜单/各 ConfigCard、release 菜单仍无 probe、`DebugProbeHostSmokeTest`/B2/Shizuku 回归。
- 遗留（本批不改）：测试源码 `backend/core/`（host 测试，package 与目录一致、mismatch=0）；`backend/cve43499` 等旧包名的 docs/analysis 历史引用保留（历史文档不得删）。

## 10 M3–M4 Lead 设备回归（2026-11-03，A301SO）

- 布局实测：`backend/{active,unfinished,declarations,host,launcher,registry,diagnostics}`；`active/{cve43499glued,cve43284glued,debugprobe}`、`unfinished/{cve43499,cve43284,lua}`；`declarations/` 只剩 BackendSpec/FlowEntryRegistryAssembly。
- package/dir mismatch：`CHECKED=449 MISMATCH=0`；旧包前缀 grep `OLD_PREFIX_COUNT=0`。
- JVM/构建：全量 **434/0/0/0**；compileDebugKotlin、assembleDebug/AndroidTest/Release 全 PASS；`git diff --check`。
- 设备（debug APK）：`DebugProbeHostSmokeTest` **OK(2)**、B2 `FlowHostProcessSmokeTest` **OK(4)**、`ShizukuFlowHostSmokeTest` **OK(4)**；fixture APK `Flow43499GluedHostSmokeTest` **OK(1)**、`Flow43284GluedHostSmokeTest` **OK(1)** ⇒ 新包名下生产 FlowSymbol/FQN 反射解析有效。
- UI：debug 菜单 6 项（含调试探针）、43499 ProfileCard 正常；**release 菜单 5 项无调试探针**；已恢复 debug+test APK。
- 未 commit；搬迁在 git 表现为 D + untracked，提交前需 `git add -A` 并 review。


## 11 2026-11-03 用户裁定：取消 active/unfinished 分区

> 原话：「没必要区分active和unfinished，统一放在backends，未完成的用注解标记」。
> 取代 §7 的 `active/` 与 `unfinished/` 两级分区；§7 只保留为历史决策记录。

### 11.1 目标结构
```
backend/
  declarations/   BackendSpec/Row/Selection、FlowEntryRegistryAssembly、@UnfinishedBackend
  host/ launcher/ registry/ diagnostics/（不变）
  backends/       所有 backend 平级放置（不再区分 active/unfinished）
    cve43499glued/   package ...backend.backends.cve43499glued
    cve43284glued/   ...backend.backends.cve43284glued
    debugprobe/      ...backend.backends.debugprobe
    cve43499/        ...backend.backends.cve43499（含 victim/flow/generator/... 子包）
    cve43284/        ...backend.backends.cve43284（含 flow）
    lua/             ...backend.backends.lua
```
- 测试镜像：`app/src/{test,androidTest}/kotlin/.../backend/backends/...`，package 同步。
- `backend/README.md` 更新：统一 `backends/`；未完工 backend 用 `@UnfinishedBackend(reason=...)` 标记（仅标记，不删除；仍不可选/灰显/保留卡与实现）。
- `declarations/UnfinishedBackend.kt`：`@Target(CLASS) @Retention(RUNTIME) @MustBeDocumented annotation class UnfinishedBackend(val reason: String)`；标注 `BackendCve43499`、`BackendCve43284`、`BackendCustom`（Lua）。
- 不变量测试：builtin 中 `selectable=false` ⇒ 必须带 `@UnfinishedBackend`；带注解 ⇒ 必须 `selectable=false`（debug probe 不标注）。
- 不删除任何代码；仅移动/package/import/FQN/注解；空目录可删。
- 门禁同 §7：package/dir mismatch=0、旧前缀=0、全量单测/构建/`git diff --check`、设备 UI/菜单/卡/probe/B2/Shizuku/fixture 回归。


## 12 M5 交付记录（2026-11-03 · 代码侧：统一 `backends/` + `@UnfinishedBackend`）

> 用户原话：「没必要区分active和unfinished，统一放在backends，未完成的用注解标记」。
> 取代 §7 的两级目录分区；§7/§9 保留为历史决策与交付记录。

- **MOVED 105 文件 / 11 个目录整体移动**：`backend/active/{cve43499glued,cve43284glued,debugprobe}` 与 `backend/unfinished/{cve43499,cve43284,lua}` → `backend/backends/…`；测试镜像 test 33、androidTest 7 同步；package 前缀统一 `com.ghostlock.app.backend.backends.<name>`（含 cve43499 全部子包、cve43284.flow、lua）。旧 `backend/active/`、`backend/unfinished/` 空目录删除（仅空目录）。
- 全量更新 main/test/androidTest package/import/FQN：`FlowSymbol` className（43499/43284/probe 均指向新 FQN）、`registry/Backends.kt` imports、注释/KDoc、测试断言字符串；未改 manifest/AIDL/攻击逻辑/profile/flow 契约。
- 新增 `declarations/UnfinishedBackend.kt`（`@Target(CLASS) @Retention(RUNTIME) @MustBeDocumented`，`reason: String`）；标注 3 个未完工 backend：`BackendCve43499`（reason=original CVE-2026-43499 backend unfinished in exec-glue build）、`BackendCve43284`（original CVE-2026-43284 backend unfinished）、`BackendCustom`（Lua/custom backend unfinished）；debug probe 不标注。
- 新增 `UnfinishedBackendAnnotationTest`（4 用例）：builtin `selectable=false` ⇒ 必带注解；带注解 ⇒ `selectable=false`；注解 RUNTIME 可见且 reason 非空、只标记 43499/43284/CUSTOM；debug probe 无注解。**负向证明**：临时移除 `BackendCustom` 注解 → 2 用例按预期 FAILED（exit 1）→ 恢复后文件 sha256 与移除前一致。
- `backend/README.md` 更新：统一 `backends/` 布局 + `@UnfinishedBackend` 政策（未完工=保留、不可选/灰显、卡保留、禁止以清理为名删除；启用=去注解+接线 flowSymbol+selectable）。
- **内容零删改证明**：备份 128 文件（105 移动 + 20 原地改 + README + 2 文档）；MANIFEST 128/128 `shasum -c` OK；对每个 app/src 文件用“备份 + 机械替换（+3 处注解插入）”前向推导比对 **125/125 IDENTITY**（README 为文档重写，单独核验）；main/test/androidTest backend `.kt` 计数 76/38/17 不变。
- **门禁（2026-11-03 本机）**：package/dir mismatch **CHECKED=451 MISMATCH=0**；`backend.active`/`backend.unfinished`（含路径形式）在 app/src **=0**；`:app:compileDebugKotlin` **BUILD SUCCESSFUL**；全量 `:app:testDebugUnitTest` **EXIT=0 / 438 tests, 0 failures, 0 errors, 0 skipped（68 XML，含新不变量测试）**；`:app:assembleDebug :app:assembleDebugAndroidTest :app:assembleRelease` **BUILD SUCCESSFUL**；`git diff --check` **EXIT=0**。
- 备份：`/tmp/ghostlock-backend-unify-backup-20261103T003709Z`（128 文件，MANIFEST sha256 `4409bfb0e03e91261388f62981e85b83f724f2f5d6d5ddf41877d9e6e7ad62a0`）；未 commit。
- **UNVERIFIED（待 Lead 设备回归）**：debug 菜单/各 ConfigCard、release 菜单无 probe、`DebugProbeHostSmokeTest`/B2/Shizuku/fixture；本轮 FlowSymbol FQN 再次变更（`...backends.*`），设备侧反射解析需复跑回归。
- 遗留（本批不改）：test `backend/core/`（host 测试，package 与目录一致、mismatch=0）；docs/analysis 中旧包名历史引用保留（历史文档不删）。

## 13 统一 `backend/backends/` + `@UnfinishedBackend` 的 Lead 设备回归（2026-11-03）

- 布局：`backend/{backends,declarations,host,launcher,registry,diagnostics}`；`backends/{cve43499glued,cve43284glued,debugprobe,cve43499,cve43284,lua}`；`declarations/UnfinishedBackend.kt` + `BackendSpec`/`FlowEntryRegistryAssembly`；README 已重写。
- 静态门禁：mismatch `CHECKED=451 MISMATCH=0`；`backend.active`/`backend.unfinished` 引用 0；全量 **438 tests/0/0/0**；compile + three assembles PASS；`git diff --check`。
- 设备 A301SO：`DebugProbeHostSmokeTest` **OK(2)**、B2 **OK(4)**、`ShizukuFlowHostSmokeTest` **OK(4)**；fixture `Flow43499GluedHostSmokeTest` **OK(1)**、`Flow43284GluedHostSmokeTest` **OK(1)** ⇒ 新 `...backend.backends.*` FlowSymbol FQN 设备侧反射解析有效；已恢复 debug+test APK。
- UI 走查本轮未重跑：设备当时锁屏（需 PIN），无法进入路径页；上一轮同一 UI 代码已走查（debug 6 项/release 5 项、卡片正常），本批仅包名/目录移动，无 UI 字符串/结构改动。
- 未 commit；git 中 D + untracked，提交前 `git add -A` 并 review。
