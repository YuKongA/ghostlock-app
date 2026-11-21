# 当前构建闭环精确设计

## Context and scope

完整迁入的有效设计/范围与验收正文；不是新增实施批。原批准/历史结果按原Beads记录，工具工件齐备不代表实现或门禁通过。

来源：`043fdf6a:.beads/issues.jsonl`；保留语义代号和原锚点。本文中的历史进度/门结果文字仅是其具名输入的沿革，当前状态从proposal列出的Beads读取。旧机制由正文已注明的后继差异取代；基线不重新授权实施。

## Decisions

<a id="record-gl-4on"></a>

## 构建闭环修复：确认精确设计与实施批准

<!-- migrated-source: 043fdf6a Beads gl-4on description -->
本节是迁入OpenSpec的现行设计/验收正文；Beads只保存执行状态、原批准和实际证据。历史来源仅作追溯。
当前为精确设计待批准；原作者/critic结论与关闭条件在原聊天。用户批准前不实施、不清理.cxx；批准后沿原矩阵验证。Linux CI NOT_RUN。旧聊天不自动commit措辞已由最新AGENTS§10取代。
原始来源（已归档/承接）：codex://threads/01a1ec39-4b71-7911-a080-05c1b7ae5fa7（2026-11-21用户优先级及最后精确设计）

<!-- migrated-source: design gl-4on -->
#### Context and scope

E-BUILD-X-001/批次01：当前构建闭环修复。以下精确范围从[原作者交付](codex://threads/01a1ec39-4b71-7911-a080-05c1b7ae5fa7)迁入仓库，供新 clone 直接读取；该聊天此前没有独立计划文件，因此本文件承接正文，而不是复制第二份仓库计划。仅将旧“不 commit”措辞按用户2026-11-21纠正同步，其余范围/判据保留。**精确实施批准仍待用户确认，迁入不授权修改/清理/运行。** 任务与状态见 [proposal](proposal.md#impact) → Beads [gl-4on](design.md#record-gl-4on)。

#### Decisions

#### 构建修复批的精确范围

##### 1. 修复 host native tests 的失效 source graph

修改：

- [build.gradle.kts](/build.gradle.kts)
- [tools/tests/CMakeLists.txt](/tools/tests/CMakeLists.txt)
- [tools/tests/diagnose_abi_test.cpp](/tools/tests/diagnose_abi_test.cpp)

具体做法：

- `nativeTestContractsHost` 不再读取已经被删除的 `app/src/main/cpp/common/**` 和 `app/src/main/cpp/backends/diagnose/**`。
- 改为读取当前真实 owner：

  - `plugin/native-source/common/common/**`
  - `plugin/packages/diagnose/src/main/cpp/**`

- CMake 明确分开 common root 与 Diagnose root。
- `diagnose_abi_test.cpp` 改为从 Diagnose root 引用 `api/diagnose_api.h`。
- 删除 root 中已经没有 caller 的旧 NDK resolver 和 `glCmakeCommon`。
- 保留 `nativeTestHost`。
- `nativeBuildAndroid` 只负责 App debug/release 两个 variant，不再暗示它验证了 JNA distribution、ELF、ABI 或插件包边界。
- 删除已经失真、且当前没有非文档 caller 的 `checkAll`；本批不再造一个新的“万能总门”。

##### 2. 修复 Diagnose 插件 package producer

修改：

- [plugin/packages/diagnose/build.gradle.kts](/plugin/packages/diagnose/build.gradle.kts)

具体做法：

- 通过 AGP 9.1 的 `androidComponents.sdkComponents.ndkDirectory` 获取**与 AGP 实际构建相同的 NDK**。
- 不再硬编码 `darwin-x86_64`。
- 在选定 NDK 的 `toolchains/llvm/prebuilt/*/bin` 中查找唯一的 `llvm-nm` 或 `llvm-nm.exe`：

  - 缺失时明确失败；
  - 出现多个候选时明确失败；
  - 不回退到 `PATH`；
  - 不猜测 host tag。

- D8、dexdump 从 AGP SDK directory 与 `buildToolsVersion` 派生，并处理 Windows executable suffix。
- release AAR 从 AGP release variant 的 `SingleArtifact.AAR` provider 取得，不再猜测固定文件名。
- 删除 `dependsOn(":plugin:api:jar")` 这种跨项目 raw task dependency，由 artifact/file provider 传播依赖。
- 保持已有 `.glplugin.zip` producer、manifest 和内联校验语义不变；本批不新增第二套 package verifier。

##### 3. 修正 CMake staging 位置

原草案想把 staging 放进 Diagnose module 自身的 `buildDirectory`，但 design-critic 查到 AGP 9.1 会对这种布局稳定发出 warning，和项目 native/build 零警告要求冲突。

修订后：

```text
仓库根 build/cxx/diagnose/**
```

它必须同时满足：

- 位于真实的仓库根 `build/**`；
- 不位于 Diagnose module 的 `build/plugin/packages/diagnose/**` 内；
- canonical path 不经过符号链接；
- 构建后源码树不再出现 `.cxx`；
- 根 `./gradlew clean` 能将其完全删除；
- 构建日志不存在 AGP staging warning 或 native compiler warning。

这里不会增加新的 staging abstraction；只是给现有 Diagnose native producer 一个合法且唯一的生成目录。

##### 4. 清除 App 中已断开的 profile/LKM build declarations

修改：

- [app/build.gradle.kts](/app/build.gradle.kts)
- [settings.gradle.kts](/settings.gradle.kts)

删除：

- 不存在的 profile-core manifest 输入；
- 已无 producer 的 LKM source/cache/generated-assets declarations；
- App 对 raw LKM generated-assets 目录的声明；
- App 中没有 caller 的旧 NDK resolver；
- 已注释、已失效的 profile-core task/dependency/include 提示。

不会删除：

- `tools/lkm/**`
- 43284 的源码或 KO
- 任何 kernel profile 实物
- LuaJ/XZ
- App 运行时逻辑
- 当前插件 generated-assets consumer

也就是说，本批只删除**虚假的构建边**，不删除后续迁移还可能需要的源材料。

##### 5. 修复 CI 中不存在的产物闭包

修改：

- [.github/workflows/build.yml](/.github/workflows/build.yml)

保留：

- APK 构建；
- APK 重命名；
- APK SHA-256；
- APK upload；
- release 中的 APK。

删除：

- 不存在的 `exportProfiles`；
- profile ZIP 打包；
- profile ZIP 哈希；
- profile ZIP upload；
- release note 中对 profile ZIP 的声明。

在真正的 provider/profile producer 存在以前，CI 只发布当前工程确实能生产的 APK。

这不会把本地 macOS 构建说成 Linux CI 已通过：GitHub Linux run 在实际执行前仍记为 `NOT_RUN`。

##### 6. 迁移并清理源码树 `.cxx`

修改：

- [.gitignore](/.gitignore)

处理方式：

- 删除历史 `app/.cxx/` 特例；
- 不增加全局 `.cxx` ignore；
- 未来若 native producer重新污染源码树，Git 会直接把问题暴露出来。

获得批准后，只删除这两处已经确认属于可恢复生成物的目录（路径相对仓库根目录）：

- `app/.cxx`
  - 约 879 个文件；
  - 约 60 MiB。
- `plugin/packages/diagnose/.cxx`
  - 约 169 个文件；
  - 约 1.7 MiB。

不会删除任何其他目录。

审查期间工作树 HEAD 被用户侧的新提交更新为 `cebb18049254ac8441a85e60c44be53cc648de4c`，其中 `.gitignore` 新增了 Beads/Dolt 规则。实施时只会定点删除旧 `app/.cxx/` 一行，保留 `.dolt/`、`.beads/**`、`*.gate.lock*` 等新规则。

##### 7. 同步当前权威文档，而不改远期设计

design-critic 发现，如果只改 Gradle，当前规范和当前态 UML 会继续宣称旧 root NDK resolver、JNA distribution 和 `checkAll` 存在，因此本批必须同步：

- [engineering-standards.md](/docs/development/engineering-standards.md)
- [full-process-uml.md](/docs/development/full-process-uml.md)

只改当前事实：

- toolchain locator 改为实际的 AGP module/`SdkComponents.ndkDirectory` authority；
- `nativeBuildAndroid` 明确只证明 App debug/release 能构建；
- 明确它不证明 JNA distribution、ELF、ABI 或插件包边界；
- 当前态 UML 删除 `checkAll` aggregate；
- UML 将 `nativeTestHost`、`nativeBuildAndroid`、debug/release XML gates 表示为分别执行。

不会：

- 改 BXP3 的批准状态；
- 改 source ledger；
- 批量重写历史记录；
- 把尚未实现的插件系统画成当前状态；
- 改远期目标 UML，因为这批只修当前构建事实，没有改变最终插件架构。

#### 明确不在本批做的事

- 不实现 `verifyGlPluginZip`。
- 不实现 APK/plugin 边界 gate。
- 不用另一个 aggregate 替代 `checkAll`。
- 不实现 provider、导入、Custom、i18n、Desktop、额外 ABI。
- 不迁移 43284、43499、98260。
- 不处理 KO producer。
- 不清 LuaJ/XZ。
- 不修 `avbcheckDevice`；它不在当前 BXP3-B1 active build closure 内，继续作为具名后续债务。
- 不升级 Gradle、AGP、Kotlin 或 NDK。
- 不改插件运行时、Flow、catalog、RunHost 或 UI。
- 不跑真机门，也不把构建成功写成产品运行成功。
- 本地 commit 按最新 AGENTS §10，在适用验证通过后主动精确提交；push / PR 仍单独触发。

因此，“构建系统修复完成”的范围严格表示：**当前 BXP3-B1 active build closure 可从 clean 状态配置、测试、构建和清理**，而不是声称仓库里每个历史/孤立任务都已经现代化。

#### 实施后的验收

按单一 build writer 串行执行：

1. 定点删除上述两个 `.cxx` 生成目录。
2. 运行根 `./gradlew clean`。
3. 静态确认 active build/CI 中不存在：

   - 旧 App native source 路径；
   - `profile-core` 构建入口；
   - `exportProfiles`；
   - `darwin-x86_64`；
   - active `checkAll`；
   - 源码树 `.cxx`。

4. 运行 plugin tests：

```text
:plugin:api:test
:plugin:core:jvmTest
:plugin:packages:diagnose:testDebugUnitTest
:plugin:packages:diagnose:testReleaseUnitTest
```

5. 运行 `nativeTestHost`，确认 moved production sources 的 host contracts 真实编译并通过。
6. 运行：

```text
:app:verifyDebugUnitTestXml
:app:verifyReleaseUnitTestXml
nativeBuildAndroid
:app:assembleDebugAndroidTest
```

7. 检查：

   - Diagnose `.glplugin.zip` producer 实际运行；
   - App debug/release 都通过同一 artifact edge 消费插件包；
   - 所有 CMake staging 位于根 `build/**`；
   - 源码目录没有重新生成 `.cxx`；
   - 没有 AGP staging warning；
   - 没有 native compiler warning。

8. 最后再次运行 `./gradlew clean`，确认 package、CMake staging 和所有 Gradle outputs 都被删除。

9. GitHub Linux CI 仍标记 `NOT_RUN`，直到 workflow 真正跑过；不会用本机结果替代 CI 证据。


#### Verification and authorization

原作者已完成自检；原独立 critic 的 staging warning 与 current-authority 文档冲突按其明确条件纳入上述设计修订。实际 warning/clean、构建及 Linux CI 证据仍待执行，不因设计审查或迁移记 PASS。本仓其他任务的批准、真机门与scope不变。运行证据/当前阻断只写 Beads，正文验收矩阵不是第二份进度表。


<a id="acceptance-gl-4on"></a>

### 验收定义与原边界：gl-4on

<!-- migrated-source: acceptance_criteria gl-4on -->
当前为精确设计待批准；原作者/critic结论与关闭条件在原聊天。用户批准前不实施、不清理.cxx；批准后沿原矩阵验证。Linux CI NOT_RUN。旧聊天不自动commit措辞已由最新AGENTS§10取代。

批准/审核/实际结果：Beads `gl-4on` notes；本段不维护实时勾选或新PASS。
