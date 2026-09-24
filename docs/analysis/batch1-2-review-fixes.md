# Batch 1/2 静态审查修复计划（2026-09-23）

> 输入：对 `ad264c8`（Batch 1）与 `6dc288e`（Batch 2）的静态审查，7 条缺陷 + 1 架构未闭环 + 1 测试缺口。
> 本文件逐条给出证据、修复方案、影响与验证，并标出需用户拍板的语义点。
> 约束：Batch 3 已完成并提交（`4b1a817`）；本修复不改变攻击关键路径语义（`cmp_disasm` 不应变化）。

## 修复项

### F1 [P1] v3 的 safe_mode 写入失效

- 证据：`toBinaryV3()` 已是当前输出；`NativeProfileDocument.safeModeOffset()` 仍按 v2 的 12 字节头计算；
  两处入口 `AndroidGhostlockRepository.kt:485`、`GhostlockUserService.kt:63` 用它直接改写 blob 的
  common 槽；而 v3 `fromBinaryV3` 在 options 段又把 `safe_mode` 覆盖回 common[67]（0）。
- 方案（推荐）：**v3 writer 不再在 options 段写 `safe_mode`**（只保留 `selected_cpus.*`），
  `safe_mode` 只经 core 的 common[67] 传输；`safeModeOffset()` 读 header 的 version 分派：
  v2 = `12 + releaseLen + 67*8`，v3 = `16 + releaseLen + 67*8`。
- 影响：v3 字节变化（options 少一条）→ `native-doc-golden.sha256` 重生成；native `parse_v3` 无需改
  （common 已含 safe_mode）；默认 `safe_mode=0` 时语义不变。
- 备选：保留 options 的 safe_mode，`safeModeOffset` 定位 options 条目（需扫描段，复杂，不推荐）。
- 验证：`safeModeOffset` v2/v3 单测、`NativeProfileDocumentTest`、golden、`ExporterAgreementTest`。

### F2 [P1] 稀疏 route 调优把未填写字段变成 0

- 证据：`ProfileMerger.fillRouteExecutionDefaults()` 见 route 子树存在即 `continue`，跳过整组默认；
  Batch 1 移除设备文件的 `include execution-<route>` 后，若导入/override 只写一个 route 字段，
  其余字段不会由 preset 补齐，编码时缺失→0。
- 方案：改为**逐字段合并**——route 组不存在则整组复制 preset；存在则对 preset 的每个字段，
  “目标缺失才补”（既有值优先）。
- 影响：修复稀疏 override/导入的部分字段场景；无 override 时结果不变（golden 不变，设备文件无
  route tuning）。
- 验证：`ProfileResolverTest`/新增 merger 测试（部分字段→其余补默认）；全量回归。

### F3 [P1] exporter 可能递归删除任意目录

- 证据：`ProfileExporter.main()` 对参数输出目录立即 `deleteRecursively()`，无任何校验。
- 方案：拒绝 `outDir == srcDir`、`outDir` 是 `srcDir` 祖先/后代、或位于源码树（`app/src`）内的路径；
  导出改为**写同级临时目录再原子替换**（`outDir.tmp-<n>` → rename），失败不破坏既有输出。
- 验证：单测拒绝 srcDir/源码树；正常导出仍成功。

### F4 [P2] v3 解码不校验组件 ID、middleware 截断

- 证据：native `parse_v3` 读取 frontend/backend 后未校验，`out->route = (uint8_t)u16` 静默截断；
  Kotlin `fromBinaryV3` 读后丢弃 frontend/backend，未知 route 落 `NoRouteConfig`。
- 方案：native 校验 `frontend==kFrontendRootChild`、`backend==kBackendCve202643499`、
  middleware 为已知 route 且 `route_fields(route) != nullptr`，否则 `-1`；Kotlin 同步把三个 ID
  读入并**精确校验**，未知即 `null`。两侧行为一致。
- 验证：`profile_binary_test` 增加错误 ID/未知 middleware 拒绝向量；Kotlin 对应测试。

### F5 [P2] 用户 CPU override 的优先级与清除不一致（**需拍板**）

- 证据：`ProfileMerger.resolveMerged` 先取 imported 的 `selected_cpus`、再取 override；
  而 `clearSelectedCpus()` 只删 override → imported 值复现。
- 需定义优先级。推荐语义（与“会话选择最高”一致）：
  `用户 override > imported > 本次 UI CPU 对`？还是 `本次 UI 对 > 配置(imported/override)`？
  现状实际是“配置优先于 UI 对”。
- 推荐：**UI 会话选择最高**（用户在主页选的 CPU 对不得被导入配置暗改，符合总计划“CPU 对等会话输入
  不得被设备推荐暗中替代”）；即 `pair` 优先于 imported/override；`clearSelectedCpus` 清除后回落 pair，
  不再复现 imported。若你希望保留“显式导入覆盖 UI”，则改为 override 优先并让清除写显式屏蔽标记。
- 验证：CPU 选择/清除路径的 controller 测试。

### F6 [P2] exporter 吞异常并可能静默漏 profile

- 证据：`parseFile()` `getOrNull()`，枚举循环 `?: return@forEach`；导出可少文件仍成功返回。
- 方案：以 `index.conf` 的 `profiles[].release/file` 为准导出；某个 entry 缺失/解析失败/编码失败 →
  抛出并使任务失败；非设备文件不再隐式参与。
- 验证：注入缺失/坏文件 → 任务报错。

### F7 [P2] `validateMerged()` 对缺失必需字段不报，且 exporter 未调用

- 证据：cred/offset 只判 `(as? Number)?.toLong() == 0L`，`null` 得 false 不报；编码器随后写 0。
- 方案：对每个必需字段区分**缺失/非数值/零**并报路径；`ProfileExporter` 在编码前调用
  `ProfileResolver.validateMerged`，失败即拒绝导出。task/number 语义按字段契约处理。
- 验证：resolver 测试（缺失 vs 零）；exporter 测试（无效 profile 拒绝）。

### F8 [测试] `ExporterAgreementTest` 目录不存在时静默通过

- 方案：改为断言导出目录存在（缺失即 fail），并让 test 任务 `dependsOn(":profile-core:exportKernelProfiles")`
  或在测试内明确要求先导出；避免单独跑测试时形同虚设。

### F9 [架构] typed 模型未进入主解析链

- 现状：`ProfileModel.kt` 的 `CoreProfile`/`ResolvedProfile` 等未参与合并；主链仍是可变 `ValueMap`
  （`ProfileMerger` + controller）。typed 解耦目标未落到运行路径。
- 建议：作为**独立跟进批次**（Batch 2.5），把 `resolveMerged`/controller 的解析与校验收敛到 typed
  `ResolvedProfile`，`ValueMap` 只留 HOCON 边界；需要新的设计文档与 golden 对照。**本修复批不处理**，
  除非你要求一并做。

## 实施顺序与验证

1. F1、F2、F4（影响 wire/合并语义，先做并重生成 golden）。
2. F3、F6、F7（exporter 正确性/安全）。
3. F5（待语义确认）。
4. F8（测试）。
5. 全量：`make -C src native-host-tests`、`./gradlew :app:testDebugUnitTest :profile-core:test exportKernelProfiles`、
   exporter 一致性；`cmp_disasm` 确认攻击函数不变（本批不改 native 执行语义）。
6. F9 另立计划。

## 进度

- [x] F1（safe_mode：v3 只经 core 槽 + `safeModeOffset` 版本分派；golden 重生成）
- [x] F2（route 调优逐字段合并；`ProfileMergerTest`）
- [x] F3（exporter 拒绝源目录/源码树 + staging→原子替换）
- [x] F4（v3 两侧精确校验 frontend/backend/middleware；`profile_binary_test` 向量）
- [x] F5（CPU 会话对优先；`ProfileMergerTest`）
- [x] F6（exporter 以 index.conf 为准、失败报错、不导出模板）
- [x] F7（`validateMerged` 缺失/类型/零；exporter 调用；`ProfileResolverTest`）
- [x] F8（`ExporterAgreementTest` 目录缺失即失败 + 测试依赖 exporter）
- [ ] F9（typed 主链）——转 Batch 2.5
- 验证：`make -C src native-host-tests`、`make -B -C src ghostlock`（零告警）、`lint-tidy` 0 findings、
  `cmp_disasm` PASS、`./gradlew :profile-core:test :app:testDebugUnitTest`。

## 待拍板（已定）

- **D1（F1）**：采用“v3 移除 options 的 safe_mode、safeModeOffset 按版本分派”（推荐），还是定位 options 条目？
- **D2（F5）**：CPU 优先级取“UI 会话选择最高”（推荐）还是“显式导入/override 最高 + 清除写屏蔽标记”？
- **D3（F9）**：typed 主链本批处理，还是另立 Batch 2.5？推荐另立。
