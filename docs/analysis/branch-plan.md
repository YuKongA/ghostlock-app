# 分支总 plan（`vr-ko-bypass-dev`）

> **唯一入口**：本文件回答「这个分支在做什么、现在到哪、下一步做什么、哪个文档算数」。
> 状态变化只改本文件的进度表；分册负责细节，不再各自维护全局顺序。
> 基线：`93afccc`。分支：`vr-ko-bypass-dev`。
> 最近更新：2026-10-05（43284 生产接线完成 + 可用性翻转；S4 配置/wire 重设计立项；third_party 清理）。

## 1. 分支目标

把 native 攻击 runtime 重塑为「顶级组件架构 + 新 backend/terminal 可插拔」，且**不破坏 43499 攻击路径**；
在此过程中接入 **CVE-2026-43284** 作为首个非 43499 backend，验证 ADR-0004 的中性性。

| 流 | 内容 | 权威文档 |
|---|---|---|
| **S1 框架收敛** | 目录/契约/offset SSOT/platform·ancillary/去绑定 | `top-level-architecture-rewrite-plan.md` |
| **S2 Steps/Terminal 重设计** | Steps 可见、`Backend<StepSet>`、统一 terminal 接口、`ActivationContext` | `terminal-steps-redesign.md`（ADR-0004 R18–R21） |
| **S3 43284 backend** | 页缓存写 + LKM/UMH 终态、生产接线、可用性 | `cve-2026-43284-backend-plan.md` + `…-assessment.md` + `…-refactor-plan.md` |
| **S4 配置/wire 重设计** | 双 backend 下的 HOCON / kprofile / wire / profile 重构（规则 R1–R6，批次 R0–R5） | `config-wire-redesign-plan.md`（**待 D1/D2 裁决**） |
| **CM 平台对策插件化** | ABI + loader + registry + 参考插件 | `countermeasure-plugin-plan.md`（ADR-0005 待写） |
| **安全网** | 分区/文件基线、AVB/verity 校验、设备端 `avbcheck` | `device-gates/` + `tools/device-guard/` |

## 2. 文档地图（现行 vs 历史）

**现行（权威）**

- 决策：`adr/0001`–`0004`（0004 含 R18–R21）；ADR-0005（CM）待写。
- 执行/顺序：**本文件**。
- 分册：`top-level-architecture-rewrite-plan.md`（S1）、`terminal-steps-redesign.md`（S2）、
  `cve-2026-43284-backend-plan.md` + 评估 + 改造计划（S3）、`config-wire-redesign-plan.md`（S4）、
  `countermeasure-plugin-plan.md`（CM）、`wire-transport-model.md`（GLKv3 格式权威）。
- 闭包状态：`architecture-findings-register.md`；审查原文：`architecture-review-log.md`。
- 门禁证据：`device-gates/*.md` + `.native.log`。
- 工程规范：`docs/development/{design-philosophy,engineering-standards,documentation-standards,adding-a-component}.md`。

**历史/参考（不作顺序依据）**：其余 `docs/analysis/*` 分册（`a2-4-5-plan`、`a3-kernelsnitch-plan`、
`minimal-lkm-plan`、`lkm-vs-root-script`、`upstream-dirtyfrag-handling` 等）。细粒度过程叙述以 git 历史为准。

## 3. 总进度

### S1 框架收敛

- [x] Phase 0（`CoreSession` 通用化，真机 P0-01 PASS）；Phase A（机械重排，真机 A3-01 PASS）。
- [x] A2-1 环境探测 → `platform::runtime`；A2-2a–e attack 模块拆空；A2-3a–c（`memory`/地址拆分/offset SSOT）。
- [x] **A2-4 全部完成**：-1 ancillary（`0f86b02`）、-2 厂商下放 `platform::vivo`（`ab92439`）、
      -3 `platform::abi` owner schema（`9ceef1c`）、-5 include 防火墙 + fake backend（`b5d7fb9`）、
      -4 修正版（`e549eeb`，中性词汇→`contract`，二进制 sha256 不变）；各批真机 PASS。
- [x] A2-5：去 backend 绑定（`504583d`/GLKv3 去 backend）；**A2-4-5** `AddressSpace` 解耦（`46224d8`，cmp 6/6 IDENTICAL）。
- [x] **身份词汇 → `contract`**（`4a06c66`）：`contract/identity.hpp`；防火墙白名单 **16→2**。
- [x] A3-1 `contract::AddressDiscovery` + kernelsnitch 可选实现（`2e9bc58`）。
- [x] B 契约/自动化同步（AGENTS/README/README_ZH/`src/core/README`/adding-a-component + 三端对拍）。
- [x] **R1 include 防火墙现状**：159 files / 4 forbidden-layer edges（`support/util.cpp→43499 backend` ×4，含 A3-2 的 `leak/address_discovery.h`，待所有权搬移后移除）/ 4 whitelisted / 0 unexpected / 0 stale。
- [~] **A3-2**：① provider → `backend/cve_2026_43499/leak/`、② `utils.h` 日志拆 `support/log.hpp` — **已完成**（`d3de876`，
      **全二进制 sha256 与改动前完全相同** `8bd6d2cd…`；防火墙 152→159 files、白名单 3→4，
      新增条目为 `support/util.cpp → leak/address_discovery.h`，`kernelsnitch.h` 的 `context_*` 非 inline 故仅此一个 TU 可包含）；
      ③ **喷雾尾部经 `AddressDiscoveryOps` 活体接线**：未落地——直接接会改失败路径语义（`last_mm_struct`/日志由 `0xffff…` 变 `0x0`），
      需先加哨兵保留分支，再以 `PRE vs POST` 逐字节同等或**完整真机泄漏门禁**验收。
- [ ] **C**：`KernelMemory` 功能（另 PR）。
- [x] **A 批 4**（`de53422`）：`stage_types` 拆分——中性 `StageResult`→`contract/stage_result.hpp`、
      后端私有 `VictimRound`/`VictimChain` → `backend/cve_2026_43499/stage_types.hpp`，删除 `session/stage_types.hpp`；
      防火墙 152 files / 0 unexpected / 0 stale；**`PRE vs A4` = 6/6 IDENTICAL strict**（B0 位移为既有漂移）。

### S2 Steps/Terminal 重设计

- [x] T0 设计 + ADR-0004 R18–R21；T1 `StepSetKind` + 三维 selection + 稀疏 catalog（真机 T1 PASS）。
- [x] T2 `TerminalExecution<T::Input>` + `ActivationContext`（与 T1 逐字节相同）。
- [x] T3a–T3d：native/Kotlin `steps` 字段、文档模板、**「一般/Shizuku/UMH」三选 UI**（`a427471`，`ExecutionModeMapping` 单一权威）。
      T3c 的 `consumer_thread` `REVIEWED_SHAPE`（`cbz w8`→`tbz w8,#0`，已带理由复核）。
- [x] T4 `Cve2026_43499Backend<StepSet>`（`91723e7`；真机 PASS：multicast ×4 → root → KernelSU）。
- [x] **T5 `umh_forward` 执行 + `RootProgram` 启动**：**已由 S3 的 B6/T5 生产接线覆盖**（见下），不再单列。

### S3 43284 backend

- [x] B0–B4：评估与 L 级计划；`capabilities`；per-backend 状态（`ec58b1a`）；identity/catalog/wire 解码（`59985fc`）；
      私有 section 7 字段（`663bc10`，manifest 94→101）。
- [x] **B5-1..B5-8**：会话秘密帧（`0de4ed8`）、`ipsec/`（`c7ded3c`）、`pagecache/`（`86b3303`）、`lkm/`（`2052b97`）、
      `steps/` ELF+Hook（`9e463b2`）与 chain（`9999aed`）、`backend_terminal`+`platform`（`27792ee`）、组合（`7aabcb2`）。
- [x] **B5-9a–B5-9g 真机分阶段链**：只读诊断 → 写原语（`written=698 verified=698`）→ 内嵌 splicehelper/libcxx blob →
      helper 写源；分阶段入口 `--run-cve-2026-43284 … --stage=plan|write|trigger|full`（生产 fail-closed）。
- [x] **B5-9h 全链 PASS（自建 LKM）**：`hook=1`、`outcome=LkmLoaded`、KernelSU 生效、SELinux 收尾回 enforcing、
      文件级守卫精确命中两处页缓存改动、**AVB `ok=12 fail=0`（`AVB_VERIFY=OK`）**。
      门禁：`device-gates/B5-9h-20261004-full-chain-lkm-pass.md`；LKM 源码 `tools/lkm/ghostlock/`。
- [x] **B6/T5 生产接线**（`e6e7daa`）：`execution_binding` 组合根注入（模块镜像/plan、单一 carrier、real chain ops、
      会话密钥、只读 UMH 就绪探测）；`umh_forward` 改为只读确认；空 plan fail-closed；`real_chain_release` 幂等。
- [x] **可用性翻转**（`869a43c`）：`backend_available(43284)` / `terminal_available(UmhForward)` /
      catalog triple 全 true；占位 CVE 仍 false。
- [x] **生产路径修复**（`76b18c1`/ `3be4622`）：carrier 探测不可见时回退首个默认（shell 域看不到 vendor 库）；
      `selinux_exec_context`/`kmi`/`lkm_path` 缺省改为派生/默认；`main` 与 backend 失败原因现在**可见**。
- [ ] **生产 app-call 真机门禁（进行中）**：staged 全链已 PASS，但**走 App 的正常 profile 路径**尚未复跑通过；
      最后一次实跑止于 `ChainRejected`（合成 SA 复现到此为止，真 SA 需 App 复测，APK 已到 v762）。
- [x] Kotlin 路由：`ExecutionMode.Mapping` 产出 `{43284, pagecache_write, umh_forward}`、会话帧随 43284 注入 stdin（`34ac98c`）。

### S4 配置/wire 重设计（**待裁决**）

- [x] **R0**：计划 `config-wire-redesign-plan.md`（规则 R1–R6、四层设计、批次 R1–R5、证据附录）。
- [ ] **R1**：policy schema + `SchemaRegistry` + `bind_all` required/default（native 内化，cmp 须 IDENTICAL）。
- [ ] **R2**：wire 原地增量（`string` 类型 + `selection` 段 + owner-qualified 路径 + manifest v4 + 生成式 Kotlin 映射）。
- [ ] **R2b**：CLI 最小化 15→8（dev 走同一 Pipeline）+ backend×terminal 矩阵过滤 UI 缝隙。
- [ ] **R3**：HOCON 布局重排（`schema_version` 恒为 1 + 旧键别名）+ 提取器输出 + `index.conf` backend 矩阵。
- [ ] **R4**：43284 policy 完整表达（路径用 string，去掉约定/token 迂回）+ UI 驱动。
- [ ] **R5**：清理 + 文档同步。
- **待裁决**：**D1** `selection` 段是否显式化；**D2** root 参数到内核侧传输（建议 LKM 读固定路径参数文件）。
- **新增需求（已入计划 §6.2）**：root 程序可选 + SELinux 恢复可选 → `session.root.*` 进 wire + Kotlin 高级设置。

### CM 平台对策插件化

- [x] **CM-1**：ABI 头 `contract/glk_cm_abi.h`（全声明/最小实现）+ C++ 映射 + 契约测试（预留项被标实现即 FAIL）。
- [x] **CM-2**：`platform/countermeasure/{loader,sha256}`（白名单/哈希/dlopen/probe）。
- [x] **CM-3**：`ancillary::RuntimeRegistry` + `Controller` 分派（阶段/优先级/gate）+ 诊断。
- [ ] **CM-4** = **R4c**：`platform/vivo/**` 删除 → 参考插件（`vr_guard`/`vr_task_tag`）+ ADR-0005（修订 R14）。
      **真实 Vivo 设备门禁未完成**（本机为 Sony），不得因 host 绿标 supported。
- [ ] **CM-5**：profile 加载清单/哈希字段 + Android assets 打包解出加载 + ABI 导出到 SDK 目录。

### 安全网 / 守卫

- [x] 分区备份（26/26，仓库外 `../ghostlock-device-backup`）+ 冷机复核 + AVB guard（基线/检查/告警）。
- [x] 设备端静态 `avbcheck`（`hash`/`baseline`/`check`/`avb-verify`（含 raw RSA）/ `verity-status`）+ 文件级页缓存守卫。
- [ ] 43284 全链后的**例行**校验清单化（每次真机门禁附带跑 `avb-verify` + `files-check`），并入 S4/R4 门禁模板。

### 清理 / 整理

- [x] `third_party/dirtyfrag` 删除（来源与 commit 记入 README「Credits & License」）；`mpack` 迁 `src/lib/mpack`（Makefile/AGENTS/文档同步）。
- [x] 源码溯源注释改为 `上游仓库@commit <path>`；现行文档同步（历史门禁与已完成计划按约定不改写）。
- [ ] 清掉 3 处 `__pycache__`（`tools/`、`tools/tests/`、`tools/device-guard/third_party/avbtool/`；已被 .gitignore 覆盖）。

## 4. 当前阻塞与风险

| # | 项 | 状态 |
|---|---|---|
| 1 | 43284 **生产 app-call 真机门禁** | 进行中；staged 全链 PASS，App 路径待复测（失败原因现在可见） |
| 2 | **D1/D2** 裁决 | 阻塞 S4 的 R1–R5 开工 |
| 3 | **Vivo 真机门禁**（CM-4/R4c） | 本机非 Vivo，无法验证；host 绿不等于 supported |
| 4 | `KERNEL-PANIC-01` | 已知环境/时序问题；判定因果需同构建复现 + 冷机复跑 |
| 5 | 备份位置 | 必须在**仓库外**（`../ghostlock-device-backup`），不得落回 `build/`（会被 `gradle clean` 清掉） |

## 5. 下一步（建议顺序）

1. **复测 43284 生产 app-call**（App 正常 profile 路径，非 staged）→ 通过则归档门禁并把 S3 收尾。
2. **裁 D1/D2** → 出 **R1 逐文件清单** → 开工 S4。
3. R4c（vivo → 插件）与 R4 视情况并行；**Vivo 门禁**显式挂未完成。
4. A3-2 与 C（`KernelMemory`）排在 S4 之后或独立 PR。

## 6. 判定标准（沿用）

- 普通改动：`make -C src native-host-tests` + NDK 零告警 + `lint-tidy` 0。
- **攻击关键路径**：`cmp_disasm`（6 组攻击函数 vs `build/native/ghostlock-B0`）+ 真机门禁（冷机、KernelSU 未加载、固定 CPU 对）+ 门禁归档。
- 新 profile 未过真机**不得**标 supported；`cmp` 无法证明不变量时停止该批并调查。
