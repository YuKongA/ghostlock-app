# 常驻 helper 桥（把每块 2 次 fork 降到 1 次会话）· 设计稿

## 1. 动机（以时长为唯一判据）

| 路径 | 每块成本 | 769 块 | 1470 块 |
|---|---|---|---|
| 现状（vendor 载体，每块 fork 读 + fork 写） | ≈50–78 ms | 38–57 s | 73–114 s |
| 目标（常驻桥，每块 1 次 IPC + 1 次 splice） | ≈1–3 ms | 1–2 s | 2–5 s |

实测依据：`2026-10-17` 真机，模块写 12304 B（769 块）≈57 s；载体复原 23512 B（1470 块）≈114 s，
两者都约 74–78 ms/块，等于 3 次 `crash_dump64` fork+exec 的成本（每次 ~25 ms）。
参照实现 `5f184760` 在同一原语上也是每块 fork（`steps/crash_dump.cpp:102`），其默认候选表四项全是 `/vendor/...`
（`chain.hpp:70-74`，不可直读）⇒ **在 vendor 载体上 ref 也达不到 3.4 ms/块**；
要达到该量级，只有两条路：① 载体可直读（`file_fd >= 0` ⇒ pread/splice，0 次 exec）；② 本设计。

## 2. 目标与非目标

- **目标**：vendor 载体下，把每块的 helper 成本从 2 次 fork 降到 1 次 IPC（目标 ≤3 ms/块）。
- **非目标**：不改 ESP 组帧、不改页来源语义（页必须仍 splice 进发送管道，不能 vmsplice 自己的内存 ——
  见 `5efb5983` 那次回退）、不改 journal/rollback 语义。

## 3. 形态

```text
父进程（app）                        子进程（patch#1 之后的 crash_dump64）
  |-- 请求管道 (w) ------------------> |  读 24 B 请求：{op, offset, path_id}
  |                                    |  op=READ  ⇒ 页内容 16 B → 应答管道
  |<-- 应答管道 (r) -------------------|  op=SPLICE⇒ 目标页 splice 进父给的管道
  |-- 数据管道 (w) ------------------> |  （splice 用：父把数据管道写端交给子）
```

- 子进程在 `session_begin` 时启动一次，`session_end` 时关闭管道并 `waitpid`。
- 每块：父写一条 24 B 请求（一次 write），子执行并回一条 16 B 或 8 B 结果。
- **fail-closed**：任一请求超时（默认 200 ms）或子进程退出 ⇒ 立刻停止后续块并把该块判失败，
  与现状「helper 失败即整段失败」一致。

## 4. 与现有代码的接缝

| 现有件 | 改法 |
|---|---|
| `landing/crash_dump_bridge.cpp:93` 的 `::fork()` | 抽成 `BridgeSession`：`open()` 一次 fork，`read16/splice16` 走管道 |
| `landing/real_splice_io.cpp` 的 helper 分支 | 改为向 `BridgeSession` 发请求（接口不变） |
| `pagecache_write.hpp` 的每块调用 | 不变 |
| `steps/pagecache_steps.cpp` 的 journal/回滚 | 不变（回滚同样走桥，仍逐块写回并回读） |

## 5. 风险与判据

- **副作用**：子进程常驻 ⇒ 需要在所有出口关闭（成功/失败/早退/异常），否则僵尸或管道泄漏；
  判据：`session_end` 后 `waitpid` 成功、无 fd 泄漏（日志打 `bridge: closed fd=… pid=…`）。
- **安全**：桥只是把「每块 fork 一次」换成「每块 IPC 一次」，写的内容与顺序不变；
  仍必须在 `restore_hook` 之后关闭（钩子撤掉后再关子进程）。
- **回归**：先保留旧路径，用 profile 开关（`bridge_mode=spawn|session`）二选一，默认 `session`，
  两路径共用同一套单测（FakeBridge）+ 一条真机对照（同载体、同模块、比较 `per_block_us`）。

## 6. 验收

1) 单测：请求/应答短读超时 fail-closed；子进程异常退出时后续块全失败；关闭幂等。
2) 真机：`write benchmark: per_block_us` 从 ≈74000 降到 ≤3000（769 块 ≤2.5 s）。
3) 语义：`written=verified`、`hook restore=true`、站点与载体状态与现状一致。

## 7. 实现后的阻塞项（2026-10-17 00:30，反汇编结论）

`BridgeSession` 已实现（默认 `bridge_mode=0`，行为不变），但**设备上尚不可用**：

- patch #1 内嵌的 helper（1432 B，DirtyFrag 一次性程序）**不读 stdin、没有会话循环**；
- 因此 `bridge_mode=1` 会在握手处被拒 ⇒ 整段 fail-closed（安全，但不变快）；
- 要真正启用，必须先产出**会话版 helper 镜像**（常驻进程，循环读 `{op,offset,path_id[,fd]}` 并回 16 B），
  再用它替换 patch #1 的 blob —— 属于新批次。

### 达到 5 s 量级的两条路（按代价排序）

| 路径 | 前置 | 预期 | 代价 |
|---|---|---|---|
| 可直读载体 | 只改 profile 的 `carrier_path`（选取逻辑已支持第一个存在的候选） | 1470 块 ≈ 5 s | 极小；但与“用参照同款 vendor 载体”冲突 |
| 会话版 helper | 新写并编译常驻 helper，替换 patch #1 blob | 1470 块 ≈ 0.1 s 量级 | 大：新增 aarch64 程序 + blob 替换 + 真机验证 |
