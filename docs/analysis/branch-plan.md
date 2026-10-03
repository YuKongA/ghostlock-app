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
- [ ] A2-3c：offset SSOT（ADR-0003 注册模型）。
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
- [ ] **T3b（下一步）**：Kotlin 字符串/布尔解析接入——`NativeProfileDocument.from` 加 `text`/`bool` 访问器；
  `steps = StepSetKind.fromToken(text("backend.steps"))?.wire ?: 0u`；调用点 `Profile.kt`/`ProfileExporter.kt`/测试改传访问器。
- [ ] **T3c**：`recommend_shizuku` 项由 `backend.steps` **取代**（移除旧项，清理 `BuiltinProfileCatalog`/`UserProfileStore`/
  `AndroidGhostlockRepository`/`GhostlockViewModel`/`GhostlockUserService`/`AndroidProfileConfigController`/`meta` 段 + 测试）；
  HOCON 资产二元项改 `true`/`false`（`backend.steps = "w1_w3"`、`vr_guard`、`compact_waiter`、`safe_mode` 等）；
  **C++ 标志字段改 `bool`**（配 `cmp_disasm` 复核，记录差异理由）。
- [ ] **T3d**：「一般执行 / Shizuku / UMH」三选 UI（替换 `shizukuEnabled: Boolean`；语义 一般=app+W1W3+root_child、
  Shizuku=shell+W1W2+root_child、UMH=umh_forward（T5 后可用））。
- [ ] T4：`Cve2026_43499Backend<StepSet>`（`W1W2`/`W1W3`）+ `attack_write` 非模板基类。
- [ ] T5：`umh_forward` 执行 + `RootProgram` 启动 + 真机。
- [ ] T4：`Cve2026_43499Backend<StepSet>`（`W1W2`/`W1W3`）+ `attack_write` 非模板基类。
- [ ] T5：`umh_forward` 执行 + `RootProgram` 启动 + 真机。

**S3 43284 backend**
- [x] 评估 + L 级计划；B0 登记。
- [x] B1a：`contract/capabilities.hpp`（`KernelMemoryOps`/`FileCacheWriteOps`）。
- [x] B1b：`RootProgram`/`TerminalInput`（现由 S2 统一）。
- [ ] B2–B7：per-backend 状态构造、identity/catalog、wire 私有 section、原语/LKM 链、UMH、Kotlin —— **S2 的 T1–T5 完成后按其落点收尾**。

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
