# P2 逐符号最小化计划（487 未用符号的五类处置）

> 分析类（≤8192 B）。**只出计划，不动代码**（执行属 native-core）。判据形式 = D1；上位 = [P2 规格](contract-layer-p2-spec.md) §7 步 4 · [gate doc](p2-compat-gate-20261008.md) §7 **U-4** · [rewrite-plan](../plan/rewrite-plan.md) §8.1。

## §1 数据集与口径

- **数据集**：`/tmp/contract-symbols.tsv`（task-91 产出，**939 符号 / 47 头**；列 = `header · kind · symbol · sig_or_value · backend_uses`）。
- **未用面**：`backend_uses == 0` 的符号 = **487**（本次复算 ✓）。
- **分类规则（互斥、按序）**：① 包含卫士宏 → 真死（注明结构件）② 命中**已落地 P4 三文件**（`pipeline/attack_window.hpp`、`pipeline/composition_root.hpp`、`contract/resource_scope.hpp`）→ P4 ③ 头属 `session/`·`profile/`·`memory/`·`contract/model*` → P3 ④ 头属 `plugin/`·`contract/abi/`·`terminal/` → P5 ⑤ 命中 `src/core/tests/**`（**含 `.disabled` 测试**）→ host 夹具 ⑥ 其余 → 真死。

## §2 五类计数（合计 = **487** ✓）

| 类 | 计数 | 判据/理由 | 代表头（未用数） |
|---|---|---|---|
| **P3 需要** | **185** | 会话/配置层（P3）将消费：模型、profile、内存布局 | `memory/target.h` 63 · `profile/glkv3.hpp` 31 · `profile/schema.hpp` 22 · `contract/model.hpp` 14 · `memory/constants.hpp` 12 |
| **P4 需要（已落地可实测）** | **0** | 方法：487 符号逐个与**已落地的 P4 三文件**全文匹配 ⇒ **0 命中**（P4 层当前只用兼容面已有符号）⇒ **如实报告，不虚增** | — |
| **P5 与插件 C ABI 面** | **115** | 插件/handoff/终端与 C ABI（解冻后使用） | `contract/abi/glk_contract_abi.h` 44 · `plugin/registry.hpp` 29 · `plugin/host_ops.hpp` 14 |
| **host 测试夹具** | **79** | 被 `src/core/tests/**`（含 `.disabled`）引用 ⇒ **恢复测试时需要** | `contract/identity.hpp` 32 · `contract/step_catalog.hpp` 9 · `support/native_resource.hpp` 8 |
| **真死** | **108** | = **46 包含卫士宏**（文件自身结构，**不可删**）+ **62 真死可删面**（见 §4 外部面核查） | `support/log.hpp` 13 · `contract/step_catalog.hpp` 10 · `platform/device_facts.hpp` 8 · `contract/identity.hpp` 7 |

## §3 可执行批次（每批 3–6 头；**判据 = 两段 gate 齐 EXIT=0 + 头计数**）

- **批次判据（统一）**：① `make -C src gate` 两段（`compat-check` 97 文件 · `window-exit-paths`）**均 EXIT=0**；② **头计数**：该批头仍在、其导出符号 ⊆ 使用点声明集，且 `backend_uses == 0` 计数按批目标下降（每批给出目标值）。

| 批 | 头（未用符号数） | 类别 | 前置 |
|---|---|---|---|
| **B-1（立即可做）** | `support/log.hpp`(13) · `support/time.h`(6) · `support/decls.hpp`(3) · `race/threads.hpp`(1) · `race/pi_race.h`(1) | 真死可删面 | 无（低风险：支持面/race 声明，不被 host 夹具引用） |
| **B-2（立即可做）** | `contract/step_catalog.hpp`(10真+9host) · `platform/device_facts.hpp`(8真+5host) · `platform/abi.hpp`(5) · `platform/runtime.hpp`(1) | 真死 + host | 无（**只删真死列**，host 列保留） |
| **B-3（立即可做）** | `contract/identity.hpp`(7真+32host) · `support/native_resource.hpp`(7真+8host) | 真死 + host | 无（同上，只删真死列） |
| **B-4（**等 P3**）** | `memory/target.h`(63) · `memory/constants.hpp`(12) · `profile/runtime_struct_offsets.h`(12) · `contract/model.hpp`(14) | P3 需要 | **P3 会话/配置层落地**（目标值 = 各自仅保留 P3 实测使用者） |
| **B-5（**等 P3**）** | `profile/glkv3.hpp`(31) · `profile/schema.hpp`(22) · `profile/document.hpp`(11) | P3 需要 | **P3 落地**（wire/绑定面） |
| **B-6（**等 P5 / 插件解冻**）** | `contract/abi/glk_contract_abi.h`(44) · `plugin/registry.hpp`(29) · `plugin/host_ops.hpp`(14) · `plugin/controller.hpp`(9) · `plugin/kernel_channel.hpp`(7) · `terminal/terminal_input.hpp`(5) | P5 / C ABI | **P5 落地 + 插件解冻**（C ABI 头为对外契约，**不得**先删） |

- **顺序理由**：B-1→B-3 的符号**已可证明**无未来消费者（外部面 0 命中，见 §4）⇒ 立即收益；B-4/B-5 的符号**已经被列进 P3 的输入规格**，先删即**返工**；B-6 是对外契约面，**只能在 P5 定稿后收**。

## §4 真死的外部面核查（**不得仅凭本仓无调用者判死**）

- **已核面**：`src/core/backend/**`（定义源）· `src/core/tests/**`（含 `.disabled`）· `app/**` · `profile-core/**` · `tools/**`（含 extractor）· `src/**` 其余目录。
- **核法**：对 62 个真死候选逐个做**词边界全文匹配**（`\b<symbol>\b`）⇒ **外部面命中 0** ⇒ 62 个列为**可删面**；46 个包含卫士宏**排除在可删面外**（结构件）。
- **未核面（**不得据此判死**）**：外部客户端（Shizuku UserService 侧、第三方调用方）· git 历史中的旧使用者 · 设备端脚本/运维面 · 文档承诺（若某符号被**对外文档**承诺为稳定 API 则属 B-6 类）。⇒ 如某批触及以上任一面，**降级为 P5 类**并重新登记。

## §5 风险与可逆恢复

- **风险**：① 删多 ⇒ backend 编译失败（gate 红，**立即停止该批**）；② 删包含卫士宏 ⇒ 重复包含/重定义；③ 误删外部使用者（§4 未核面）⇒ 只能靠对方报错发现（**故先做 B-1→B-3**）；④ 与 P2 收口批的并发写冲突 ⇒ 同一时刻**单写入流**（AGENTS）。
- **可逆恢复（逐层）**：① **符号级** = 从同名 `.disabled` 孪生件取回该声明（`src/core/<path>.disabled` 与活件**同源**）；② **文件级** = 从孪生件 diff 回填；③ **批次级** = `git revert`（**不删 `.disabled`**、不改禁用清单，故恢复面永不丢失）。
- **每批复核**：`make -C src gate` 两段 + 头计数（§3 判据）+ 未用计数下降值记录到 [rewrite-plan](../plan/rewrite-plan.md) §8.1 的 U-4 行。

## §6 未决

| # | 未决 | 依赖 |
|---|---|---|
| U-1 | **P4 类为 0** 是否意味着 P4 层未消费任何旧声明面（需在 P4 收口时复核一次） | P4 收口批 |
| U-2 | 「外部客户端/第三方」是否存在真实调用方（§4 未核面） | 用户/维护者确认；确认前**不删**相关符号 |
| U-3 | B-4/B-5 的目标值（每头保留多少符号） | **P3 落地后的使用点清单** |
| U-4 | C ABI 头的对外承诺范围（哪些符号必须保留） | **P5 定稿 + 插件解冻裁定** |
