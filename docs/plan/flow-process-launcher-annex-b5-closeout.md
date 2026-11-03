# Flow 进程启动器计划 · 附录 B5 收口：迁移范围、遗留与回收策略

> 状态：**收口说明（2026-11-02，B5c-3 部分）**；归属主计划 v5。本附录只记录范围/遗留/策略，不新增执行契约。
> 依据：主计划 v5 §4–§8、`annex-b4a`、`annex-b4b`、`annex-b4-real`、G9–G13 设备报告。

## 1 迁移范围（exec-glue build 事实）

- 可选择的 exec-glue backend 只有 **43499-Glued / 43284Glued**；两者均已 `flowSymbol` + `runner=null`，走新进程 Flow 通道（B4a/B4b）。
- 原版 43499/43284/98260/Custom Lua 均为 **不可选**（`selectable=false`/`runner=null`，原版 native targets 不构建）。因此：
  - **B5a 98260 迁移不适用于当前构建**：其 UI 不可选，且迁移会改动 98260 payload/finish 证据与独立计划；重新启用该 backend 时必须先补 flowSymbol/entry/registry 与真机门禁。
  - **B5b Lua 迁移不适用**：Lua 不可选；动态 backend 目前没有 host 侧 symbol 注册路径（host 进程只见启动时 `Backends.all` builtins），见 §3 gap。
- 旧 Shizuku host 基础设施（`ShizukuBackendHost(s)`/`ShizukuBackendClient`/旧 AIDL host 方法）目前仅被 **98260 legacy（不可选）** 代码引用，保留为已知技术债；不阻塞新通道。

## 2 B5c 状态

- B5c-1 已完成：删除旧 in-process Flow runner / 双执行体（`BackendFlowRunner`、`FlowLaunchParent` 旧执行器、`BackendSpec.runner/appOwned`），dispatcher 只剩 `launchSymbol`；全量单测 470/0/0。
- B5c-2 已完成：删除 43499/43284 旧 ELF exec 通道（`ExecutableBackendProcess`/`ExecutableShizukuHost`/stager/env/protocol/UTF-8 assembler/`Glued*ExecutableHost`/旧 Flow 类 + 旧测试）；`ShizukuBackendHost` 仅留 98260 分支。
- B5c-3 残留（不进入新进程路径，仅静态/文档债）：
  1) 98260 legacy Shizuku host/AIDL host 方法（不可选 backend；删除会级联 98260 execution/payload 代码，留待重新启用或单独 B5a）；
  2) Lua/动态 backend 的 host 侧 symbol 注册 gap（需静态预登记入口或跨进程注册设计，当前无 selectable Lua）；
  3) UML §11.9 已更新；approval/pointer 收口随本附录。
- 旧通道删除均仓外备份：`/tmp/ghostlock-b5c1-backup-20261102T195528Z`（MANIFEST `a833dab0…`）、`/tmp/ghostlock-b5c2-backup-20261102T200303Z`（MANIFEST `8ae0a112…`）。

## 3 A11 keepalive 回收策略（当前=有意保留，无 watchdog）

- 触发：43284 entry 超时/取消 ⇒ `host_keepalive=true`；host 保留进程/runtime/run_dir，跳过清理与自杀；G11 用诊断 flow 设备验证，G12 用 slow fixture 在**生产 43284 entry + 真实 IpSecRuntime**上验证（timeout⇒`runtime-leaked`，payload 未被 kill）。
- 当前策略：**不做自动回收**（避免在 payload 可能仍存活时拆 IPsec/杀进程）；回收手段=设备重启/`pm clear`/人工 kill + run_dir 手工清理。已知后果：少量 keepalive host 进程与 run_dir 会跨 run 保留。
- 后续候选（未实现，需 L 评审）：app 启动时扫描 `filesDir/ghostlock-glue/flowhost/` 下超过保留期（如 24h）且无存活 host 的 run_dir 清理；不得清理活跃 host 的 run_dir，不得 kill payload。实现前保持“人工/重启回收”。
- Shizuku 侧：G13 已改为 keepalive 时 `unbind(remove=false)`（保留 shell UserService/进程），真机语义属 B3b 未验证；`unbind(remove=false)` 的进程累积同样受“人工/重启回放”约束。

## 4 未验证总表（截至 2026-11-02）

| 项 | 状态 | 归属 |
|---|---|---|
| 真实 43499/43284 漏洞 ELF 冷机门禁（B4-real） | **待用户明确授权** | annex-b4-real |
| Shizuku flavor 真机（B3b：registry/Context/IpSec/keepalive/unbind） | 设备无 Shizuku 包 | B3b |
| release APK 下 :flowhost/攻击流程 | 仅启动 smoke | B4-real/后续 |
| 98260/Lua 迁移与旧 host 删除 | 不适用/技术债 | B5a/B5b/B5c-3 |
| keepalive 自动回收 watchdog | 未实现（有意保留） | 本附录 §3 |
| 绑定窗口内取消计时、Shizuku tag 竞态 | 代码/编译证据 | B3b/G13 |

当前 goal「实现新进程攻击机制」的**机制层**（symbol+params→新进程→反射执行 Flow、App 通道真机、keepalive/取消/超时/A11）已交付并验证；剩余主要是真实攻击授权与 Shizuku 环境验证。
