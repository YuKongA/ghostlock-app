# 分支总 plan（`vr-ko-bypass-dev`，2026-10-03）

> **唯一入口**：本文件回答「这个分支在做什么、现在到哪、下一步做什么、哪个文档算数」。
> 状态变化只改本文件的进度表；分册负责细节，不再各自维护全局顺序。
> 基线：`93afccc`。分支：`vr-ko-bypass-dev`。

## 1. 分支目标

把 native 攻击 runtime 重塑为「顶级组件架构 + 新 backend/terminal 可插拔」，且**不破坏 43499 攻击路径**；
在此过程中接入 **CVE-2026-43284** 作为首个非 43499 backend，验证 ADR-0004 的中性性。

三条并行工作流：

| 流 | 内容 | 权威文档 |
|---|---|---|
| **S1 框架收敛** | Phase 0/A/A2/A3/B/C：目录、契约、offset SSOT、platform/ancillary、去绑定 | `top-level-architecture-rewrite-plan.md` |
| **S2 Steps/Terminal 重设计** | Steps 可见、`Backend<StepSet>`、统一 terminal 接口、`ActivationContext` | `terminal-steps-redesign.md`（ADR-0004 R18–R21） |
| **S3 43284 backend** | 页缓存写 + LKM/UMH 终态、Kotlin 选择 | `cve-2026-43284-backend-plan.md` + 评估 |

## 2. 文档地图（现行 vs 历史）

**现行（权威）**

- 决策：`adr/0001`–`0004`（0004 含 R18–R21）。
- 执行/顺序：**本文件**。
- 分册：`top-level-architecture-rewrite-plan.md`（S1）、`terminal-steps-redesign.md`（S2）、
  `cve-2026-43284-backend-plan.md` + `cve-2026-43284-backend-assessment.md`（S3）。
- 闭包状态：`architecture-findings-register.md`。
- 审查原文（含被推翻）：`architecture-review-log.md`。
- 门禁证据：`device-gates/*.md` + `.native.log`。

**历史/分册参考（不作为顺序依据）**：`5x-tcp-geometry-plan`、`ancillary-*`、`extract-*`、`fallback-route-wire`、
`flexible-kernel-rw-primitive`、`gradle-test-decoupling`、`host-attack-dataflow`、`kernel-phys-offset`、
`placeholder-backend-survey`、`profile-editor-complete-fields`、`vr-guard-plan`、`exploit-session-generalization-plan`（Phase 0 已并入 S1）。

## 3. 总进度

**S1 框架收敛**
- [x] Phase 0：`CoreSession` 通用化；真机 `P0-01` PASS。
- [x] Phase A：A1 + 批 2 + 批 3a–3e（机械重排）；整体真机 `A3-01` PASS。
- [x] A2-1：环境探测 → `platform::runtime`。
- [x] A2-2a–e：attack 模块拆空（timer→support、bootstrap→backend、root script→terminal、primitive→backend）。
- [x] A2-3a：`in_direct_map` → `memory`，删除 `attack/`。
- [x] A2-3b：`AddressSpace` / `ResolvedAddresses` 拆分（布局不变）。
- [ ] A2-3c：offset SSOT（ADR-0003 注册模型）。**已执行 -1/-2/-3**（提交 `d131197`/`6e6d155`/下一提交）：Document+Schema 影子 bind、43499 owner Schema 迁 parse、生产 strict + 三端 manifest（58/58 资产 strict 通过）；**-4（物理抽离）并入 A2-4**。**裁决**：-1..3 只做声明/校验所有权与 bind，不动 `kernel_offsets`/`TargetProfile` 物理布局（保 8 攻击函数 byte-identical）；物理拆分留 -4/A2-4。
- [ ] A2-4/5：platform/ancillary 分层；传输/入口去绑定；`common.h` 去耦。
- [ ] A 批 4（可选）：`stage_types` 拆分 + route 契约归位。
- [ ] A3：kernelsnitch 拆分（保留许可）。
- [ ] B：契约/自动化同步（README/adding-a-component/AGENTS、Kotlin/wire 对拍）。
- [ ] C：`KernelMemory` 功能（另 PR）。

**S2 Steps/Terminal 重设计**
- [x] T0：设计 + ADR-0004 R18–R21。
- [x] T1：`StepSetKind` + 三维 selection + 稀疏 catalog + 测试（真机 `T1` PASS）。
- [x] T2：`TerminalExecution<T::Input>` + `ActivationContext`（二进制与 T1 逐字节相同，T1 真机覆盖）。
- [x] T3a：native wire `steps`（backend 私有 section）+ `main` 使用 + 真机 `T3` PASS；Kotlin `steps` 字段 + `StepSetKind` 枚举。
- [x] **T3b**：Kotlin `text`/`bool` 访问器 + `backend.steps` 字符串解析；`value` 放参数末位保住尾随 lambda 调用点；
  `:profile-core:test` 13/0 通过、`:app:compileDebugKotlin`/`compileDebugUnitTestKotlin` exit 0（子智能体 A 执行）。
  注：T3b 代码随文档提交 `2fdf961`（当时误用 `git add -A` 收进），提交信息不含 T3b，内容无误。
- [x] **T3c**：文档/模板（子智能体 D，`d3b41ce`）；协议代码+资产+extractor（子智能体 C）。
  门禁（委派子智能体）：host/NDK/lint/`profile-core:test`/`app:testDebugUnitTest`(83)/`cargo test`(40) 全 PASS；
  `cmp_disasm --reviewed`：`do_one_write` OPERAND-SHIFT；**`consumer_thread` 单条 `cbz w8`→`tbz w8,#0`**
  系 `optional<uint8_t>::value_or(0)!=0`→`optional<bool>::value_or(false)` 的必然结果（寄存器/指令数/分支目标不变），
  已作为**显式带理由的 `REVIEWED_SHAPE`** 写入 `tools/cmp_disasm.py` 并复核，`RESULT: PASS`。真机 3 次冷机 2 PASS/1 FAIL(W1 flake) → PASS，记录 `T3c-…-direct-pass.md`。
- [x] **T3d**：「一般 / Shizuku / UMH」三选 UI（提交 `a427471`；`ExecutionModeMapping` 单一权威；UMH 置灰；未按 mode 覆写 profile 的 `backend.steps`）。
- [x] T4：`Cve2026_43499Backend<StepSet>`（`W1W2`/`W1W3`）+ `Cve43499Primitives` 非模板基类（提交 `91723e7`；host/NDK/lint/cmp PASS；`attack_write` 符号改 `Cve43499Primitives::*`，cmp 加拼写）。真机 `T4` PASS（一次冷机，multicast ×4→root→KernelSU）。
- [ ] T5：`umh_forward` 执行 + `RootProgram` 启动 + 真机。

**S3 43284 backend**
- [x] 评估 + L 级计划；B0 登记。
- [x] B1a：`contract/capabilities.hpp`（`KernelMemoryOps`/`FileCacheWriteOps`）。
- [x] B1b：`RootProgram`/`TerminalInput`（现由 S2 统一）。
- [x] B2：per-backend 状态构造（提交 `ec58b1a`；`BackendState` 概念 + `Pipeline` RAII；host/NDK/lint/cmp PASS；真机 `B2` PASS（一次冷机））。
- [x] B3：43284 known-but-unavailable identity/catalog/contract/wire 解码（提交 `59985fc`）。
- [x] B4：`backend.cve_2026_43284` 私有 section（7 字段，数值 policy token；strict 仅 backend==6 接受；manifest 94→101；提交 `663bc10`）。
- [~] B5：43284 原语/LKM 链。**设计已完成**：`cve-2026-43284-b5-design.md`（519 行，参考 DirtyFrag-Android-Root-Jailbreak/LSPromise/DFReroot/DirtyInit；选定 IpSecManager+CBC-16B+crash_dump+libc++ sentry+DFRoot LKM/UMH 链；B5-0..B5-7 分批）。
  **硬前置**：① 运行时参数通道（GLK1 v2 仅 u64，载不下 IpSec SPI/ports/32B+32B 密钥）需决策；② 需可审计且与 KMI 匹配的 `dirtyfrag.ko` + 可加载设备（公开发布 `.ko` 与源码不一致）；③ `BackendExecution` 需泛化到 `TerminalInput`（R10 延续），`platform/abi|vivo` 不存在。**未获资产前不实现、不标 supported**。
- [ ] B6：`umh_forward` 执行 + `RootProgram` 启动 + 真机（依赖 B5）。
- [x] B7：Kotlin backend 选择（`BackendKind` 含 43284 wire 6/available=false；HOCON `backend.kind`；UI 置灰；偏好迁移）——Gradle exit 0、golden 未动。
- 教训：委派重试前先确认原 subagent 确实结束（`send_message` 返回 not found 不可靠）；本轮出现两个 B7 并发写同一 Kotlin 文件，已中断重试并未造成损坏。

### T3c 精确范围（只读调研结论，2026-10-03）

**移除 `recommend_shizuku`**：
- native：`model.h:83`、`binary.cpp:63`（`kMeta` 表）、`profile_binary_test.cpp:96,117`；native 无其它消费者。
- Kotlin：`NativeProfile.kt`（:23 属性、:90 序列化、:393 `from`、:487/:512 Builder、:659 build）；
  `BuiltinProfileCatalog`（:15/:31-33/:65）、`UserProfileStore`（:102-112）、`AndroidProfileConfigController`（:778/:791）、
  `ProfileResolver.KnownTopLevel`（:14）、`AndroidGhostlockRepository`（:159-175/:927/:954）、`GhostlockModels.KernelSnapshot`（:19）、
  `GhostlockViewModel`（:87/:92/:98-103/:830）、`GhostlockUserService`（:47-52）、相关测试 5 个。
- HOCON：62 个 assets（56×`=0`、2×`=1`：`…ab14110541.conf:6`、`…ab14546557.conf:5`；4 模板 `=null`；`6.1.145-maybe-dirty.conf:29`）。
- Extractor/doc：`tools/extract_rs/src/report.rs:319`（`=0`）与 :449；README/PROFILE_SCHEMA/SUPPORTED_DEVICES/templates。

**HOCON 布尔项实际只有**：`recommend_shizuku`（移除）、`compact_waiter`（`route.*` 段，25 文件）、`recommend_vr_guard`（1 文件）。
`safe_mode` **不在 HOCON**（仅 wire `meta.safe_mode`，`patchSafeMode()` 运行时改写）——branch-plan 之前把它列为 HOCON 项有误，已修正。

**native 标志改 `bool`**（候选，须保 `sizeof`/尾部 padding 与攻击代码 byte-identical）：
- `ProfileMeta::safe_mode`（model.h:85）、`KernelMisc::vr_guard`（:123）、`KernelMisc::compact_waiter`（`optional<bool>`，:118）；
- 连带：`binary.cpp:12-28/61-68/116-122/192-197`、`model.h:308-314` 访问器。

**bool 解析缺口（必须一并修）**：`getLongAt` 只认 `Number`；当前 `from()` 用 `vu("recommend_vr_guard")`（NativeProfile.kt:394）与
`vbOrNull("compact_waiter")`（:443，经 `nativeValue` 分支映射），改 T/F 后会**静默丢值** → 换 `bool` 访问器，并让 `compact_waiter` 分支查找支持布尔。
## 4. 唯一执行顺序（下一步）

```
已完成：T1 → T2 → T3a
接着：  T3b → T3c → T3d → T4 → T5
并行：  B2（组合根 per-backend 状态构造，43499 机器码不变）
随后：  A2-3c → A2-4/5 → S3 B3–B7 → A3 → B → C
```

- **默认下一步 = T3b**（Kotlin `text`/`bool` 解析接入；不改 native 行为）。
- T3b→T3c→T3d 必须**按序**：先有解析访问器，才能移除 `recommend_shizuku` 与改 HOCON T/F；UI 最后消费。
- T4 在 T3d 之后（`Backend<StepSet>` 需要 `steps` 已能从 profile 正确到达）。
- B2 可与 T3b–T3d 并行；A2-3c 是 S3 剩余项的前置。
- 每批门禁：host + NDK + lint；触攻击路径/公共契约加 `cmp_disasm` + 真机并归档；
  `cmp_disasm` 按 ADR-0004 第九轮定位（必跑、记录差异理由、允许有理由的机器码变化）。

## 5. 当前基线

- HEAD `93afccc`；工作树按批次提交；`build/native/ghostlock-B0` 为攻击函数基线。
- 已通过的整体真机证据：`P0-01`、`A3-01`、`A2-1`、`A2-2a`、`A2-2b`、`A2-2c`、`A2-2d`、`A2-2e`、`A2-3a`（第 3 次）、`A2-3b`、`B1b`、`T1`、`T3`（native）。
- 设备：A301SO / `5.15.189-android13-8-00016-…-ab14546557`；冷机、固定 CPU 对 0/1；adb serial `QV770MFGJ1`。

## 6. 约束与保留

- ADR-0004 R1–R21（含 R18–R21）；错误分层 E1–E3；所有权 O1–O5；控制流 CF1–CF5。
- GLK1 v2 容器中性、不改版本号；profile 唯一配置权威；`kernelsnitch/` 上游冻结边界。
- 新计划/分册出现时，**只在本文件登记一行并给出权威指向**，不再各自维护全局顺序。

## 7. 变更记录

- 2026-10-03：建立分支总 plan；合并 S1/S2/S3 顺序。
- 2026-10-03：T1/T2/T3a 完成；T3 拆为 T3a–T3d，明确 **T3b→T3c→T3d→T4→T5** 的顺序；HOCON T/F + C++ bool 并入 T3c。
