# 分支总 plan（vr-ko-bypass-dev）

> **唯一入口**：本文件回答「这个分支在做什么、现在到哪、下一步做什么、哪个文档算数」。
> 状态变化只改本文件的进度表；分册负责细节，不再各自维护全局顺序。
> 基线：93afccc。分支：vr-ko-bypass-dev。
> 最近更新：**2026-10-05**（S4＝**只有 R0**；43284 app 域直连打通；契约层 α–δ 完成；LKM 通道端到端 PASS；构建环境移出 iCloud）。

---

## 0. 状态总览（做了 / 没做）—— 只改这一节即可反映全局

### 0.1 ✅ 已完成 + **有真机门禁证据**

| # | 项 | 证据 |
|---|---|---|
| 1 | S1 框架收敛（A2-4 全部、身份词汇→contract、A2-5、A 批 4、A3-1、A3-2①②③） | device-gates/（P0-01/A3-01/A3-2…）；防火墙 159 files / 4 edges / 0 unexpected / 0 stale |
| 2 | S2 Steps/Terminal（T0–T4；T5 由 S3 的 B6 覆盖） | 真机 T1 PASS；T4 真机 PASS（multicast ×4 → root → KernelSU） |
| 3 | S3 B0–B6 生产接线 + 可用性翻转（869a43c）+ 生产缺陷修复（76b18c1/3be4622） | — |
| 4 | S3/B5-9h 全链（自建 LKM；AVB ok=12 fail=0） | device-gates/B5-9h-20261004-full-chain-lkm-pass.md |
| 5 | S3 **43284 生产 app-call 门禁**（adb 免 App 试验台，EXIT=0） | device-gates/43284-production-20261005-pass.md（8b4e037） |
| 6 | 契约层 **α/β/γ**（能力词汇/接口、Capabilities 进会话、ABI 归一 + plugin/ 收口） | 1e1dac8 / 83d2df8 / 662a5f2；β、γ 各跑过 43499 真机门禁 |
| 7 | 契约层 **δ-1/2/3**（LKM 版本化通道 + 窗口接线 + 会话绑定驻留） | delta-lkm-channel-20261005-pass.md、delta3-lkm-session-20261005-pass.md |
| 8 | **δ-4 插件活化**（Capabilities → glk_contract_ops、窗口内 POST_TERMINAL 同步派发、fail-soft） | delta4-plugin-lkm-e2e-20261005-pass.md（真 .so 插件 calls=4） |
| 9 | **43284 app 域直连**（设备事实降级 + umh 探针域感知 + Kotlin 撤销强制 Shizuku） | 真机日志：device_facts degraded=proc_version,selinux；starting native:（非 UserService） |
| 10 | 安全网：分区备份（仓库外）+ 设备端 avbcheck + 文件级页缓存守卫 | tools/device-guard/；历次门禁附带的 AVB 12/0 |

### 0.2 ✅ 已完成（仅 host / NDK / lint / Kotlin 测试）

| # | 项 | 证据 |
|---|---|---|
| 11 | Kotlin/profile-core 门禁 + 会话帧逐字节对拍 | :app:testDebugUnitTest、:profile-core:test 绿；native session_frame.cpp ↔ Kotlin SessionSecretFrame 一致 |
| 12 | **构建环境修复**：Gradle 输出移出 iCloud（build 符号链接；输出落 ~/.ghostlock/build） | 重复副本 0；javac/dex 不再 defined multiple times；.gitignore 已含 build.nosync/、/build |
| 13 | γ 批目录合并（plugin/）与类型名收口（Ancillary* → Plugin*） | d2a526d；真机 43499 PASS |

### 0.3 ❌ 未做（按优先级）

#### S4 配置/wire 重设计 —— 只有 R0（计划），R1–R5 一行未写

- [x] **R0** 计划 + ADR：config-wire-redesign-plan.md（384 行，R1–R8、批次 R0–R5、证据附录）
- [x] **R1**（`38f1343`）：`FieldSpec` 增 `default/source/wire/doc` + `DefaultValue{Literal|Derived|Convention}` + `SchemaRegistry` + `bind_all(registry,mode)` + `default_used=` 诊断；
      默认/推导逻辑从 backend 上收（`selinux_ctx`/`kmi`/`lkm_path`/`late_load_args`/`carrier_path`/root package）。**wire 零改动**（manifest 101 字段不变）。
      **真机门禁 PASS**（`device-gates/s4-r1-20261005-pass.md`）：43499 冷启 + 43284 app-call；解析值 `kmi=5015 / lkm_path=<HOME>/helper.ko / selinux_ctx=0 / late_load_args=0` 与 pre-R1 **逐项一致**；AVB 12/0。
- [x] **R2**（`7bc6eb2`）：wire 原地增量——**owner-qualified 段名**（`backend.cve_2026_43499.*` / `platform.abi.*` / `common.*` / `countermeasure.vivo_vr_guard.*`；`backend.cve_2026_43284.*` 不变）+ `string` 类型（上限 256）+ **选区感知文档** + **manifest v4**（7 列，101 字段）+ Kotlin **生成式**映射。
      **核心目标达成**：**43284 文档 13 段 → 2 段**（仅 `backend.cve_2026_43284` + `common`）；43499 = 17 段。
      **真机门禁 PASS**（`device-gates/s4-r2-20261005-pass.md`）：43284 app-call `EXIT=0` + `profile_resolved` 值不变 + `lkm_window opened=1`；43499 冷启 PASS；**负向：1.2 时期旧段名文档被拒**；AVB 12/0。
- [x] **批 A · 版本统一为 3**（`43713de`）：63 个资产 `schema_version = 3`；Kotlin **写出恒 3**、**唯一迁移点 `LegacyProfileConverter.normalizeSchemaVersion`**（接受 3；legacy `1` 与**缺键**归一为 3 并记 stderr 诊断；其它值拒绝）；extractor 产出 3；App `1.3`。
      门禁：host 44 ok / 防火墙 172-0-0 / NDK 0 告警 / lint 0 / Kotlin 测试 EXIT=0 / `cargo test` 41 passed。
- [x] **批 B · R2c v3-only 割接**（`acaa0ac`）：删除 `profile/binary.cpp`（v2 framer+writer）、`-DGHOSTLOCK_ENABLE_V2_WRITER`、v2 manifest（`profile-manifest.tsv`）与 4 个 v2 测试/测试块；`entry.cpp` 非 map 根直接拒；新增「v2 被拒」host 用例。
      **真机门禁 PASS**（`device-gates/s4-r2c-20261005-pass.md`）：43284 app-call `EXIT=0`；43499 冷启 PASS；**真机喂 v2 旧 bin → `EXIT=255` 被拒且不 panic**；AVB 12/0。host 59 ok / 防火墙 171-4-4-0-0 / NDK 0 / lint 0 / Kotlin EXIT=0 / cargo 41 passed。
- [x] **批 B2 · R2c-2**（`84d0564`）：`orchestrator.hpp` 判据改 `contract::BackendKind::Cve2026_43284` + **删除 `profile/binary.h`**（framing 迁 `ghostlock::profile`）+ 删 Kotlin v2 codec（`toBinary/fromBinary`/`nativeDocumentV2`；`patchSafeMode` 保留为 GLKv3 委托）+ 迁移 v2 白盒测试。
      **真机门禁 PASS**（`device-gates/s4-r2c2-20261005-pass.md`）：43284 app-call `EXIT=0`（值不变）、43499 冷启 `EXIT=0`、AVB 12/0；防火墙 **170**-4-4-0-0；判据 `grep toBinary|fromBinary|binary_profile::`（非 tests）为空。
- [x] **R4（提前）· 43284 policy 完整表达**（`66ab498`）：`WireKind::String` 端到端接通（`ReadResult` 持有解码缓冲、≤256 B fail-closed）+ 三字段改 string 路径（token 表删除）+ 握手参数入文档（`wait_timeout_ms=15000`/`module_poll_attempts=40`/`module_poll_interval_ms=5`）；manifest **104 字段**。
      **真机门禁 PASS**（`device-gates/s4-r4-20261005-pass.md`）：默认路径值不变；**移走默认 ko 后自定义 `lkm_path` 仍跑通（证明策略由文档承载）**；不存在路径 binding 失败 `EXIT=255` fail-closed；43499 冷启回归 PASS；AVB 12/0。
      （原条目：）把 `backend.cve_2026_43284.*` 的
      `carrier_path`/`lkm_path`/`defex_symbol` 由 **uint 选择器 token** 改为 **`string` 路径**（`string` 类型已在 R2 就绪），
      并把现硬编码的握手/等待参数（`wait_timeout_ms=15000`、`module_poll_*`）搬进文档；UI 高级设置驱动。
      **顺序修正（2026-10-05 取证）**：实测 `profile-manifest-v3.tsv` 里这三个字段仍是 `uint` + token 语义，
      而 R2b 要删的 10 个 `--cve43284-*` 策略标志正是它们的 CLI 覆盖入口 —— 先删 CLI 会失去设置自定义路径的手段。
      门禁：真机 43284 app-call，用**文档里的自定义 `lkm_path`/`carrier_path`** 跑通（证明策略已由文档承载）。
- [ ] **R2b** CLI 最小化 15→8（dev 走同一 Pipeline，删 `--run-cve-2026-43284`/`--stage`/`--probe-*`/`--cve43284-*`）
      + backend×terminal 矩阵过滤 UI 缝隙。**依赖 R4**：策略必须已能由文档完整表达。
- [ ] **R3** **HOCON 布局重排**（schema_version 恒为 1 + 旧键别名归一化）
      - [ ] 目标形态：顶层 ghostlock{ schema_version, release, selection{backend,steps,terminal}, execution{}, platform{abi{},vivo{}}, backend{ cve_2026_43499{route{},execution{},…}, cve_2026_43284{…} } }
      - [ ] 68 个内置 conf 机械迁移；index.conf 增 **backend 矩阵**（backend/steps[]/terminal[]/available，与 native catalog 对拍）
      - [ ] tools/extract_rs 改输出新布局；别名表让旧键/用户导入 profile 不改也能跑
- [x] **R4 已上移至 R2b 之前**（见上方条目；原顺序为 R2b → R3/R4，取证后修正）。
- [ ] **R5** 清理 token 间接层 + PROFILE_SCHEMA/README/AGENTS 同步
- [ ] **R8 剩余**：SHA-256 仍两份（plugin/sha256.* + backend/cve_2026_43284/ipsec/hmac_sha256.*）→ 上移 support/
- **待裁决**：**D1** 已由维护者回答（owner-qualified；43499 收进 backend{}，43284 独立段）；**D2** root 参数到内核侧传输（建议 LKM 读固定路径参数文件）—— **未决**
- 新增需求（计划 §6.2）：root 程序可选 + SELinux 恢复可选 → session.root.* 进 wire + Kotlin 高级设置 —— **未做**

#### CM 平台对策插件化

- [x] CM-1/CM-2/CM-3（ABI、loader/sha256、registry+controller）—— γ 批后落在 plugin/
- [ ] **CM-4 = R4c**：platform/vivo/** 删除 → 参考插件（vr_guard/vr_task_tag）+ ADR-0005；**真实 Vivo 设备门禁未完成**（本机 Sony），不得因 host 绿而标 supported
- [ ] **CM-5**：profile 加载清单/哈希字段 + assets 解包 + ABI 导出到 SDK 目录

#### 契约层剩余

- [ ] **插件系统 = 外部 `.so` + 四投影契约**（设计 §3.14.6，维护者 2026-10-05 明确）：
      - [x] **native C ABI 投影**（部分）：`glk_entry`/`glk_module` + `glk_contract_ops` 导出 + 窗口内同步派发（δ-4）
      - [ ] **wire/HOCON 投影**：`plugin.<id>.{enabled,stage,module_path,module_hash,params.*,extract.*}` + 统一 `plugin.conf`（依赖 R2）
      - [ ] **Kotlin 投影（P1）**：导入 `.so`（文件选择器 → 私有目录 → SHA-256 清单）、**经 native 探针**读取自描述（不在 JVM 内 dlopen）、
            按 `params_schema` 渲染高级设置并做类型/必填/默认值**校验**、生成 `plugin.<id>.*` 到文档
      - [ ] **extractor 投影（P2）**：`tools/extract_rs` 认识插件**特有提取参数**（`extract_schema`：符号/偏移/结构），校验并产出 `plugin.<id>.extract.*`
      - [ ] **native 探针**：`--plugin-probe <path>` 输出机器可读描述（Kotlin 校验与 extractor 共用同一份）
      - [ ] **RuntimeInfo**（含**当前 backend**）供插件按 backend 适配
      - [ ] **P3 参考插件** = CM-4（vivo → 插件）
- [ ] **App 域对 /dev/glk 的可达性**：untrusted_app 对 null_device 的 ioctl 未验证（LKM 窗口在 app 域当前 opened=0，已被 fail-soft 兜住）

#### 其它

- [ ] **C**：KernelMemory 功能 + support/util.cpp 拆分（现为防火墙 4 条白名单的唯一成因）
- [ ] 43284 全链后的**例行**校验清单化（每次真机门禁附跑 avb-verify + files-check）并入门禁模板
- [ ] 清掉 3 处 __pycache__（已被 .gitignore 覆盖）
- [ ] **App 正常 profile 路径**最终确认：app 域直连已越过「设备事实」「终端探针」两处阻塞，**待维护者在 App 内点一次「一般执行 + 43284」确认全链**

### 0.4 阻塞 / 风险

| # | 项 | 状态 |
|---|---|---|
| 1 | **D2**（root 参数到内核侧的传输） | 未决 → 阻塞 R4 的 root 参数化部分 |
| 2 | **Vivo 真机门禁**（CM-4/R4c） | 本机非 Vivo，无法验证；host 绿 ≠ supported |
| 3 | KERNEL-PANIC-01 | 已知环境/时序问题；因果判定需同构建复现 + 冷机复跑 |
| 4 | 备份位置 | 必须在**仓库外**（../ghostlock-device-backup），不得落回 build/ |
| 5 | iCloud 同步 | 仓库在 ~/Documents 同步区；Gradle 输出已移出，**Make/NDK 输出仍在 build/（符号链接）** |

---

## 1. 分支目标

把 native 攻击 runtime 重塑为「顶级组件架构 + 新 backend/terminal 可插拔」，且**不破坏 43499 攻击路径**；
在此过程中接入 **CVE-2026-43284** 作为首个非 43499 backend，验证 ADR-0004 的中性性。

| 流 | 内容 | 权威文档 |
|---|---|---|
| S1 框架收敛 | 目录/契约/offset SSOT/platform·ancillary/去绑定 | top-level-architecture-rewrite-plan.md |
| S2 Steps/Terminal | Steps 可见、Backend<StepSet>、统一 terminal 接口、ActivationContext | terminal-steps-redesign.md（ADR-0004 R18–R21） |
| S3 43284 backend | 页缓存写 + LKM/UMH 终态、生产接线、可用性、app 域直连 | cve-2026-43284-backend-plan.md + 评估 + 改造计划 + 43284-production-adb-harness.md |
| S4 配置/wire 重设计 | 双 backend 下的 HOCON / kprofile / wire / profile 重构 | config-wire-redesign-plan.md（**R0 已完成**） |
| 契约层重设计 | 能力词汇/虚接口/ABI 归一/LKM 通道/插件活化 | contract-design.md |
| CM 平台对策插件化 | ABI + loader + registry + 参考插件 | countermeasure-plugin-plan.md（ADR-0005 待写） |
| 安全网 | 分区/文件基线、AVB/verity 校验、设备端 avbcheck | device-gates/ + tools/device-guard/ |

## 2. 文档地图（现行 vs 历史）

**现行（权威）**：adr/0001–0004（ADR-0005 待写）；执行顺序＝**本文件**；分册 top-level-architecture-rewrite-plan.md(S1)、
terminal-steps-redesign.md(S2)、cve-2026-43284-*.md(S3)、config-wire-redesign-plan.md(S4)、contract-design.md(契约层)、
countermeasure-plugin-plan.md(CM)、wire-transport-model.md(GLKv3 格式权威)、**profile-pipeline-flow.md（全流程 UML）**；
门禁 device-gates/*；规范 docs/development/*。

**历史/参考**：其余 docs/analysis/*；细粒度过程以 git 历史为准。

## 3. 分期进度（细节）

### S1 框架收敛
- [x] Phase 0 / A；A2-1、A2-2a–e、A2-3a–c；**A2-4 全部**；A2-5；身份词汇→contract（白名单 16→2）。
- [x] A3-1 AddressDiscovery；B 契约/自动化同步；**A3-2①②③**（③ 真机门禁 PASS）。
- [x] A 批 4：stage_types 拆分。
- [ ] **C**：KernelMemory 功能 + support/util.cpp 拆分（另 PR）。

### S2 Steps/Terminal
- [x] T0–T4（T4 真机 PASS）；T5 由 S3/B6 覆盖。

### S3 43284 backend
- [x] B0–B4；B5-1..B5-8；B5-9a–B5-9g；**B5-9h 全链 PASS（AVB 12/0）**。
- [x] B6/T5 生产接线（e6e7daa）+ 可用性翻转（869a43c）+ 生产修复（76b18c1/3be4622）。
- [x] 生产 app-call 门禁 PASS（adb 免 App 试验台）+ 修复 hook guard / 终点标记 / 试验台脚本权限。
- [x] **app 域直连修复**（3700b9e / eee7755 / 961e93e）：设备事实**降级**（degraded=proc_version,selinux,…）、
      umh_forward 就绪探针**域感知**（不可观测 ≠ 未就绪）、Kotlin 撤销「43284 强制 Shizuku」。
- [ ] App 正常 profile 路径最终确认（待维护者点一次）。
- [ ] App 域 LKM 窗口：/dev/glk 对 untrusted_app 不可开（fail-soft 兜住）；如需可用需单独标签决策。

### 契约层重设计（contract-design.md）
- [x] α（1e1dac8）、β（83d2df8，43499 真机 PASS）、γ（662a5f2）、δ-1/2/3（98b7c0c/d9c8ab7/f5077a8）、δ-4（7751550，端到端 PASS）。
- [x] 裁决：resident 默认常驻 + **会话绑定卸载**（fd 关＝窗口结束；定时器降为 60s 泄漏 watchdog）；插件调用**同步**；对策 **fail-soft**。
- [ ] **插件自描述**（§3.14）：RuntimeInfo + 参数 schema + HOCON/wire plugin 大项（统一 plugin.conf）。

### S4 配置/wire 重设计
- [x] **R0**（计划）。
- [x] **R1**（`38f1343`）：schema 权威 + `SchemaRegistry` + `bind_all` default/required + `default_used` 诊断；**wire 零改动**；真机门禁 PASS（`device-gates/s4-r1-20261005-pass.md`）。
- [ ] R2 / R2b / R3 / R4 / R5（展开见 §0.3）。

### CM 平台对策插件化
- [x] CM-1/CM-2/CM-3；目录经 γ 批并入 plugin/。
- [ ] CM-4（=R4c，Vivo → 插件，Vivo 门禁未完成）、CM-5。

## 4. 判定标准（沿用）

- 普通改动：make -C src native-host-tests + NDK 零告警 + lint-tidy 0；跨语言改动另加 :app:testDebugUnitTest / :profile-core:test。
- **攻击关键路径**：**真机门禁**（冷机、KernelSU 未加载、固定 CPU 对）+ 门禁归档 —— **唯一权威判据**；cmp_disasm 为可选诊断。
- 新 profile / 新可用性未过真机**不得**标 supported。
