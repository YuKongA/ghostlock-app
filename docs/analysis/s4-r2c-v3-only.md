# S4 · R2c 实施设计：**v3-only 割接**（native/extractor 只认 3；旧配置走 Kotlin legacy；v2 弃用）

> 决策（维护者 2026-10-05）：**extractor 与 native 只认最新版**；**旧配置统一由 Kotlin `LegacyProfileConverter` 转换**；**旧 bin（wire v2）弃用**。
> 前置：R2（owner-qualified wire）已落地。版本规则见 `AGENTS.md`「版本号统一为 3」。

## 1. 目标

1. **native 只认 `schema == 3`**：非 map 根 / `schema != 3` → **直接拒绝**（错误带实际值），删除 v2 回退；
2. **删除 v2 读与 v2 writer**（含 host-only 开关）；
3. **Kotlin 是唯一迁移点**：`LegacyProfileConverter` 负责「旧 HOCON `schema_version = 1`」与「旧 `offsets.json`」→ 3；
   其余版本值一律拒绝并报实际版本；
4. **extractor 只产出/只认 3**（`report.rs` 已改为 3）；
5. 文档与 manifest 同步：删掉 v2 专属产物。

## 2. 逐文件清单

**native（删除/收敛）**
- `src/core/profile/entry.cpp`：去掉 `binary_profile::frame`（v2）回退；v2 与未知一律拒绝；
- `src/core/profile/binary.{hpp,cpp}`：**删除**（v2 framer + v2 writer）；`looks_like_glkv3` 若在此则迁入 `glkv3_parse`；
- `src/Makefile`：移除 `-DGHOSTLOCK_ENABLE_V2_WRITER`；清理相关目标/测试；
- `src/core/README.md`：删除 v2 相关段落；
- 测试：删除 v2 专属（`profile_manifest_test` 等），保留/新增「v2 被拒」的负向用例（断言拒绝码与错误信息）。

**Kotlin / profile-core**
- `profile-core/.../NativeProfile.kt`：移除 v2 文档/写路径（若存在）；
- `app/src/test/resources/profile-manifest.tsv` + `ProfileManifestAgreementTest`：**删除**（v2 ABI manifest 已无消费者）；
   保留 `profile-manifest-v3.tsv` + `ProfileManifestV3AgreementTest`（并按其新形态更新注释，不再写 “v4”）；
- `LegacyProfileConverter.kt`：作为**唯一迁移点**，负责 `schema_version = 1` → 3（并记诊断）；
- App 加载路径：写恒 3；读 `1` 走转换；其它值拒绝；`ProfileMerger` 写 `schema_version = 3`。

**资产**
- `app/src/main/assets/kernel_profiles/*.conf`：`schema_version = 1` → `3`（63 个；被 include 的 5 个片段不带该键）。

**文档**
- `docs/analysis/wire-transport-model.md`：v2 段落改为「**已弃用**（native 只认 3）」；保留 v1/v2 历史说明但标注废弃；
- 本计划 + `branch-plan.md`：登记 R2c 与门禁。

## 2b. R2c 批内**无法**完成、留给 **R2c-2** 的两项（写范围所限，2026-10-05 实测）

1. **native：`profile/binary.h` 只能瘦身、不能删除**
   原因：`src/core/pipeline/orchestrator.hpp`（R2c 写范围外）include 了它，并用 `binary_profile::kBackendCve202643284`（=6）判断 43284。
   R2c 的处理：保留「仅含组件 id 常量/谓词」的瘦身头、删除 `binary.cpp`（v2 framer + writer）。
   **R2c-2**：把 `orchestrator.hpp` 该处改为 `contract::BackendKind::Cve2026_43284`（一行），随后**整头删除**。
2. **Kotlin：App 侧仍在用 v2 codec**
   实测调用点：`app/src/main/.../Profile.kt:79/94`、`AndroidProfileConfigController.kt:561`、
   `GhostlockUserService.kt:70`、`AndroidGhostlockRepository.kt:457/668`（`NativeProfileDocument.toBinary/fromBinary/patchSafeMode`）。
   R2c 的处理：**保留**这些 API（否则 `app/src/main` 编译红），只删 v2 manifest（`profile-manifest.tsv`）+ 其测试 + 仅被该测试使用的 `declaredWireKeys()`。
   **R2c-2**：把这 4 处调用迁到 v3（或删除死路径），然后删除 `NativeProfileDocument.toBinary/fromBinary` 与 `Magic/Version`。

**R2c-2 完成判据**：`grep -rn` 搜 `toBinary|fromBinary|binary_profile::`（范围 `app/src/main` 与 `src/core`，排除 tests）**为空**；且 `profile/binary.h` 已删除。


### 2c. R2c-2 逐文件清单（取证：v2 codec 的消费者**只有测试**）

**native**
- `src/core/pipeline/orchestrator.hpp`：`binary_profile::kBackendCve202643284` → `contract::BackendKind::Cve2026_43284`（一行）；
- 删除瘦身后的 `src/core/profile/binary.h`（R2c 已把 v2 framer/writer 从其中移走）；
- 清理残留 `binary_profile::` 引用（grep 确认为空）。

**Kotlin**（需把 `app/src/main/**` 纳入写范围）
- 删除 `Profile.toBinary()` / `Profile.fromBinary()`（`@VisibleForTesting`，仅测试用）；
- 删除 `AndroidProfileConfigController.nativeDocumentV2()`（`@VisibleForTesting`，注释自述「Retained for the frozen native-doc-golden.sha256 and v2 tests」）；
- 删除 `NativeProfileDocument.toBinary()` / `fromBinary()` 与 `Magic` / `Version` 常量；
- **保留** `NativeProfileDocument.patchSafeMode(...)`：它作用于 **GLKv3 字节**，是 3 处生产路径（`GhostlockUserService:70`、`AndroidGhostlockRepository:457/668`）的活代码；注释里的 "v2:" 措辞陈旧，应一并改。

**测试迁移（6 个文件）**
- **删除**：`NativeDocumentEquivalenceTest`（冻结的 v2 字节金标）与 `app/src/test/resources/native-doc-golden.sha256`；
- **迁到 v3**（`nativeDocument(config)` + `Glkv3Decoder.decode` 取代 `nativeDocumentV2` + `fromBinary`）：`BuiltinProfilesTest`、`Sog10ProfileRegressionTest`、`ControllerOverrideTest`；
- **改写为 v3 往返**：`ProfileRoundTripTest`（`Glkv3Encoder`/`Glkv3Decoder`）；
- 保留 `native-doc-golden-v3.sha256`。

## 3. 不变量

- 攻击路径**行为不变**（43499/43284 真机门禁）；v2 只在「拒绝」路径上变化；
- v3 编码/解码字节不变；manifest v3 字段数不变（仅注释/路径按 R2 形态）；
- 旧配置可迁移：给一份 1.2 时期的 HOCON（`schema_version = 1`）→ App 能读入并转为 3 运行；
- 无新增可变全局；零告警；防火墙不变。

## 4. 门禁

| 门槛 | 命令/判据 |
|---|---|
| host | `make -C src native-host-tests`（含「v2 被拒」负向用例 + 防火墙） |
| NDK / lint | 零告警 / 0 findings |
| extractor | `(cd tools/extract_rs && cargo test --release)`（产出 `schema_version = 3`） |
| Kotlin | `:app:testDebugUnitTest`、`:profile-core:test`（含「旧 1 → 转换 3」用例） |
| 真机 43499 | 冷启 + `--load-prebuilt-profile`（v3 bin）→ root + KernelSU ready |
| 真机 43284 | app-call（adb 试验台）→ EXIT=0 + `profile_resolved` 与 R1 一致 + `lkm_window opened=1` |
| 负向 | 喂一份 v2 旧 bin → **拒绝**（错误可见），不 panic、不误当成 v3 |

## 5. 风险

- 删除 v2 后，**任何仍指向旧 bin 的路径**（调试转储、历史门禁脚本）都会失败 → 文档/脚本同步；
- `profile-manifest.tsv` 删除会牵动 `ProfileManifestAgreementTest`；必须一起删，否则测试红；
- `platform/abi.hpp` 的 owner schema **保留**（v3 绑定仍用它）——删的只是 v2 **serializer** 与其 manifest。
