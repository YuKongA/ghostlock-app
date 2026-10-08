# 操作词表（op 词表细则）

> 分析类（≤8 KB）。配套 [ops](handoff-payload-ops.md)（判据/诊断/gate 插入点）、[plan](handoff-payload-plan.md) §3、[b1b2](handoff-payload-b1b2-runtime.md) §B1.1、[queue-schema](handoff-payload-queue-schema.md)。**本文是「op 词表与逐 op 事实」的唯一权威**。**自包含，不引用任何外部计划**。

## 1. 操作词表（修正版）

| token | 入口（文件:行） | 写模式 / 目标 | route 依赖 | 重试来源 | 前置 / 后置可观测 |
|---|---|---|---|---|---|
| `probe.selinux` | `runtime::check_selinux_off()`（:330） | 只读 | 无 | 无 | true ⇒ 跳过 W1（:359-363） |
| `write.selinux` | `retry_write_stage<M>`（:342-348） | `Zero`(1)→`data_alias(selinux_enforcing)`（:345） | alias 几何 | `w1_attempts`（multicast=1，:337-340） | `verify_selinux_stage`（:348） |
| `repair.scratch` | `w1_scratch_repair<M>`（:289-323） | `Zero`→heap page+`mcast.buffer_size`（:294-307） | `route.multicast_waiter.*`（:293） | `w1_scratch_repair_attempts`（:301-303） | 前置 `quarantine_reclaim_sockets()`（:296）；后置释放（:317） |
| `spawn.victim` | `victim::spawn_victim(pipes)`（:138） | 派生 victim | 无 | W2/W3 round（:386-394） | `child_task` 非 0（:143-146） |
| `write.cred` | `retry_write_stage<M>`（:164-168） | `Credential`(2)→`child_task+task_cred_off`（:165） | cred 几何 | `w2_attempts`/`w2_settle_us`（:166-167） | 前置 `w2_fast_repair_prebuild`（:89）；后置 `verify_w2_stage`（:168） |
| `probe.leaf` | `retry_write_stage<M>`（:214-216） | `Zero`→`child_task+task_comm_off`（:215） | **仅非 tcp**（:207/:212） | **硬编码 4/50000**（:215，D24） | `verify_leaf_dir_stage`（:216）；失败弃 child（:222-223） |
| `write.seccomp.flags` | `attack_write<M>`（:242-244） | `Zero`→`flags_target`（thread_info.flags，:230-232） | flags 几何 | `w3_attempts`（:237-238） | 写成功；须与下一枚**背靠背**（:251） |
| `write.seccomp.mode` | `attack_write<M>`（:253-255） | `Zero`→`mode_target`（:233-235） | 同上 | 同上（共用 attempt 循环） | `verify_seccomp_probe_stage`（:270） |
| `probe.seccomp` | `waitpid(...,WNOHANG)`（:264）+探针（:270） | 只读 | 无 | chain round（`w3_chain_rounds`，:385） | `chain.seccomp_ok=1`（:271） |
| `park` | `park_retry_child`（:111-127） | 写 `"P"`（:117）→存 parked child（:119-120） | 无 | round>1（:387） | `parked_victim*` 就绪（:119-120） |
| `handoff.root_child` | `run_root_child_handoff`（root_child.cpp:41-104） | settle（:51）→`"G"`（:59/:67）→探针（:85-86） | 无 | **无**（保持**一枚**） | `KernelSU ready`（:96）或具名降级（:97-102）；`!ever_rooted⇒Failed`（:53-55） |
| `payload script` | `write_root_script`（root_script.cpp:19；调用点 `cve_2026_43499_backend.cpp:72`）+child（root_child.cpp:59/:67） | 生成脚本+root shell | 无 | 无 | 退出码+状态（root_child.cpp:91-103） |

> 未注明文件的行号均在 `steps.cpp`。

> 行号口径：未注明文件的行号均在 `steps.cpp`（词表由 ops §1 拆分而来，内容不变）。
