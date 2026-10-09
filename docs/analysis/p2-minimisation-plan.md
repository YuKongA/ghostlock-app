# P2 逐符号最小化计划（487 未用符号的五类处置）

> 分析类（≤8192 B）。**只出计划、不动代码**。判据 = D1；上位 = [P2 规格](contract-layer-p2-spec.md) §7 步 4 · [gate doc](p2-compat-gate-20261008.md) §7 **U-4** · [rewrite-plan](../plan/rewrite-plan.md) §8.1。

## §0 执行时机（**2026-10-08 重排**）

- **整体后移**：**P3/P5 落地 + `.disabled` 参考件退役后**才开始（此前只留方法、不动代码）。
- **前置不变式**：凡被 `.disabled` 或 `docs/**` 引用的符号 = **「为新实现预留的接口」⇒ 不得删**（`.disabled` 是新实现的**输入规格**，非历史垃圾）。
- **当前可删面 = 0**；§2/§3 的 **24** 仅作**方法演示**，**不是**可执行批次。
- **撤销登记（按物证）**：**B-1..B-3 曾于工作树执行 24 项删除**（**未提交、未进入任何提交**）⇒ 经 Lead 复核判定**违反 R39**（**24/24 命中 `.disabled` 或 `docs` 引用**）⇒ **全部从 `.disabled` 孪生件回退**；**最终净效果 = 代码层零变更**（`git diff -- core/src` 为空）；**无提交涉及删除**。
- **根因（脚本缺项，供后续实现者校核）**：① `.disabled` 因**扩展名过滤**被整类静默跳过（`*.hpp.disabled` 不匹配 `*.hpp`）；② `docs/**` **从未走查**。修脚本后复扫三批 = **0 可删**（与 24/24 命中一致）。
- **物证（回退前实测）**：`git diff --numstat -- src` = B-1 后 `1/2 race/pi_race.h` · `3/3 race/threads.hpp` · `2/2 support/decls.hpp` · `12/61 support/log.hpp`；B-2/B-3 后再加 `5/39 contract/identity.hpp` · `2/8 contract/step_catalog.hpp` · `1/1 platform/device_facts.hpp` · `3/4 support/native_resource.hpp`；三批回报附**逐项 span**（`hexdump L188/28`、`set_proc_name L171/3`、`step_spec_from_token L83/7`、`route_name L218/8`）；Lead 抽验符号一度 **in-tree = 0**（`gettime_ns`/`write_file`/`waiter_thread`…），回退后 **= 1**。⇒ 事实 = **「门禁 + 复核在提交前拦下违规删除」**。

## §1 数据集与口径

- **数据集**：`/tmp/contract-symbols.tsv`（task-91，**939 符号 / 47 头**；列 = `header · kind · symbol · sig_or_value · backend_uses`）。
- **未用面**：`backend_uses == 0` 的符号 = **487**（本次复算 ✓）。
- **分类规则（互斥、按序）**：① 卫士宏 → 真死 ② 命中**已落地 P4 三文件** → P4 ③ 头属 `session/`·`profile/`·`memory/`·`contract/model*` → P3 ④ 头属 `plugin/`·`contract/abi/`·`terminal/` → P5 ⑤ 命中 `tests/**`（**含 `.disabled`**）→ host ⑥ 其余 → 真死。

## §2 五类计数（合计 = **487** ✓）

| 类 | 计数 | 判据/理由 | 代表头（未用数） |
|---|---|---|---|
| **P3 需要** | **185** | 会话/配置层（P3）将消费 | `memory/target.h` 63 · `profile/glkv3.hpp` 31 · `profile/schema.hpp` 22 · `contract/model.hpp` 14 |
| **P4 需要（已落地可实测）** | **0** | 方法：487 符号逐个与**已落地的 P4 三文件**全文匹配 ⇒ **0 命中**（P4 层当前只用兼容面已有符号）⇒ **如实报告，不虚增** | — |
| **P5 与插件 C ABI 面** | **115** | 插件/handoff/终端与 C ABI（解冻后使用） | `glk_contract_abi.h` 44 · `plugin/registry.hpp` 29 |
| **host 测试夹具** | **79** | 被 `tests/**`（含 `.disabled`）引用 ⇒ 恢复测试时需要 | `contract/identity.hpp` 32 · `step_catalog.hpp` 9 |
| **真死** | **108** | = **46 包含卫士宏**（结构件，**不可删**）+ **24「方法演示值」**（双层扫描实测；**当前可删面 = 0**，见 §0） | 仅作方法演示（B-1 13 · B-2 3 · B-3 8） |

## §3 可执行批次（每批 3–6 头；**判据 = 两段 gate 齐 EXIT=0 + 头计数**）

- **批次判据（统一）**：① `gate` 两段（`compat-check` 97 文件 · `window-exit-paths`）**均 EXIT=0**；② **头计数**：导出符号 ⊆ 使用点声明集，且未用计数按批目标下降。

| 批 | 头（未用符号数） | 类别 | 前置（**已重排**：一律等 §0 时机） |
|---|---|---|---|
| ~~B-1~~（**已撤销**） | `support/log.hpp`（13，方法演示） | 真死可删面 | **等 P3/P5 落地 + `.disabled` 退役**（§0） |
| ~~B-2~~（**已撤销**） | `contract/step_catalog.hpp` + `platform/device_facts.hpp` + `platform/abi.hpp` + `platform/runtime.hpp` 合计 3（方法演示） | 真死 + host | **同上** |
| ~~B-3~~（**已撤销**） | `contract/identity.hpp` + `support/native_resource.hpp` 合计 8（方法演示） | 真死 + host | **同上** |
| **B-4**（等 P3） | `memory/target.h`(63) · `memory/constants.hpp`(12) · `profile/runtime_struct_offsets.h`(12) · `contract/model.hpp`(14) | P3 | **P3 落地**（目标值 = 仅留 P3 实测使用者） |
| **B-5**（等 P3） | `profile/glkv3.hpp`(31) · `profile/schema.hpp`(22) · `profile/document.hpp`(11) | P3 | **P3 落地**（wire/绑定面） |
| **B-6**（等 P5/解冻） | `glk_contract_abi.h`(44) · `plugin/registry.hpp`(29) · `plugin/host_ops.hpp`(14) · `plugin/controller.hpp`(9) · `plugin/kernel_channel.hpp`(7) · `terminal/terminal_input.hpp`(5) | P5/C ABI | **P5 + 解冻**（对外契约，不得先删） |

- **⚠ 双层扫描（批次前置，强制）**：① **内层** = 同文件**声明之外**引用 · **同层头**（含 `.disabled`）· 同层 `*.cpp.disabled`；② **外层** = `tests/**`（**含 `.disabled`**）· `app/**` · `profile-core/**` · `tools/**` · `docs/**`。任一层命中 ⇒ **不得删**。
- **引用判定口径（**保守口径，显式写入**）**：双层扫描把**注释与文档中的提及也算作引用** —— 实例：`support/log.hpp` 的 `ASSERT` 被移出真死面，其唯一「外部引用」是 `log.hpp:8` 的**注释文本**（`SYSCHK / ASSERT macros and …`）；而 `COLOR_*` 的排除是**真代码使用**（`:99/:103/:106/:109` 的宿主分支 `printf`）。⇒ **真死计数 = 下界**（实际可删面**可能更大**）；这是**刻意的安全偏置**，与 **R40（注释 = 证据）** 一致；**不得**把 24 读作「已穷尽」。
- **事故记录（2026-10-08）**：粗扫给「62 可删面」，**双层扫描后实测 24** —— 差额 **38** 去向 = 同文件自用 / 同层头（含 `.disabled`）/ host 夹具 / 文档引用。⇒ 38 个**曾误判为可删**，未执行删除，已冻结。
- **顺序理由（重排后）**：所有批次**一律等 §0 时机**；B-4/B-5 的符号已进 P3 输入规格、B-6 属对外契约面 ⇒ 先删即返工。

## §4 真死的外部面核查（**不得仅凭本仓无调用者判死**）

- **已核面**：`backend/**`（定义源）· `tests/**`（含 `.disabled`）· `app/**` · `profile-core/**` · `tools/**` · `src/**` 其余。
- **核法**：62 候选做词边界全文匹配 ⇒ 外部面命中 **0**（当时口径）；46 卫士宏**排除**。
- **未核面（不得据此判死）**：外部客户端（Shizuku/第三方）· git 历史 · 设备端脚本/运维面 · 对外文档承诺；命中 ⇒ **降级 P5 类**。

## §5 风险与可逆恢复

- **风险**：① 删多 ⇒ gate 红（停止该批）；② 删卫士宏 ⇒ 重复包含；③ 误删外部使用者（§4 未核面）⇒ 对方报错才发现；④ 并发 ⇒ **单写入流**。
- **可逆恢复（已被验证有效）**：① 符号级 = 从同名 `.disabled` 孪生件取回声明（**本次 24 项即如此回退**）；② 文件级 = 从孪生件 diff 回填；③ 批次级 = `git revert`（不删 `.disabled`）。
- **每批复核**：`gate` 两段 + 头计数；未用计数下降记入 rewrite-plan §8.1 U-4。

## §6 未决

| # | 未决 | 依赖 |
|---|---|---|
| U-1 | **P4 类为 0** 是否意味着 P4 层未消费任何旧声明面（需在 P4 收口时复核一次） | P4 收口批 |
| U-2 | 外部客户端/第三方是否有真实调用方（§4 未核面） | 确认前**不删**相关符号 |
| U-3 | B-4/B-5 目标值（每头保留多少符号） | **P3 落地后使用点清单** |
| U-4 | C ABI 头对外承诺范围 | **P5 定稿 + 插件解冻裁定** |
