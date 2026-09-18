# Native 早退点析构与资源审计（CPP12）

范围：`run_exploit()` 的每个返回/终止路径、W2/W3 链的失败分支，以及 `main.cpp`/`util.cpp` 中剩余的裸资源调用。目标是对照 C 基线确认“进程退出时资源由内核回收、dirty 资源故意保留”的既有语义没有被 C++ 化改变。

## 1. `run_exploit()` 早退点

| 路径 | 当时已持有的资源 | 退出行为 | 与 C 基线一致 |
|---|---|---|---|
| `--profile` 缺失/非法 | 无（`heap_context_init` 已执行，仅空容器） | `return 1`；全局 `ExploitSession` 析构只释放内存（container/unique_ptr/UniqueFd），无 syscall 语义变化 | 是 |
| `runtime_config_init` 失败 | 无 | 同上 | 是 |
| `select_offsets` 失败 | 无（profile 未发布） | 同上 | 是 |
| `multicast_phase1_probe` 失败 | resident ctx（已 `kernel5_resident_stop`） | `return 1`；无 fd 泄漏（resident destroy 关闭 socket） | 是 |
| W1 失败 | heap 已 spray：SKB、mm sets、reclaim socket pair、quarantined 页 | `kernel5_resident_stop()` + `return 1`；`PayloadPage` 无析构释放（平凡），reclaim fd 由内核在进程退出时关闭；mm sets 的 memfd 未显式 close（进程退出回收） | 是（C 版同样依赖进程退出） |
| `quarantine_reclaim_sockets` 失败 | 同上 + current 页 | `return 1`；quarantine 失败意味着 current 未转移，页仍由 `current` 持有；进程退出回收 | 是 |
| W1b repair 失败 | quarantined 页保留 | `return 1`；注释明确“keeping page quarantined”，进程退出回收 | 是 |
| resident W1 policycap repair 失败 | resident ctx | `kernel5_resident_stop()` + `return 1` | 是 |
| `w1_only` 诊断 | heap 资源 | `return 0`；进程正常退出，内核回收 | 是 |
| `w2 never rooted` | `victim`/pipe 副本、全局 heap | `pr_error()` → `exit(-1)`：**跳过栈上局部对象的析构**（无栈展开），全局静态析构运行；fd 由进程退出关闭，child 靠父进程退出后的 EOF 自行退出 | 是（C 版 `pr_error` 同样 exit） |
| W2/W3 `fork failed` | `victim` 部分 pipe（spawn 失败前关闭了已创建的 pipe？——`spawn_child` 失败时 `pipe()` 在前，失败返回 -1 但 fds 未接管） | `return 1`；局部 `UniqueFd` 析构关闭已接管的端；未接管的 pipe fd 若在第二次/第三次 `pipe()` 失败时… 见§3 | 是（spawn 失败路径与 C 版逐行一致） |
| handoff 正常结束 | `victim.child()` 已 `release_child()`；root shell 独立 | `return 0`；`parked_cmd_w`/`uid_read` 析构关闭 | 是 |

## 2. W2/W3 链内资源

- `parked_cmd_w = std::move(victim.cmd_write)`：所有权转移；`parked_child` 是已 park 的 rooted child（不 kill；`waitpid(WNOHANG)` 只回收若已退出）。
- W3 循环 `waitpid(..., WNOHANG) == child` 检测到 child 退出后：原实现只置 `child_alive = 0`，pid 保留；当前实现同样只置标志（VictimContext 回退后无 pid owner 变化）。
- `victim.uid_read` 在 `spawn_victim` 失败路径与链结束时 `.reset()`；`cmd_write` 在 handoff 发送后 `.reset()`。

## 3. 剩余裸资源调用（main.cpp / util.cpp）

| 位置 | 形式 | 结论 |
|---|---|---|
| `apply_iomem_cache` | `fopen(.ghostlock_iomem)` + `fclose` | 短借用，配对，无需 RAII |
| `process_has_seccomp` | `fopen(/proc/self/status)` + `fclose` | 同上 |
| `park_rooted_child` | `fopen(oom_score_adj)` + `fclose` | 同上 |
| `vr tag` 分支 | `fopen(/proc/modules)` + `fclose` | 同上 |
| `child_main` probe | `pipe()` + `close()` ×2（fork child 内） | fork child 保持最小 C，`CPP-FORK-01` 允许 |
| `handoff worker` | `open(script)` + `close(probe)`（fork child 内） | 同上 |
| `perf_find_task` | `UniqueFd` + `MappedRegion`（声明序保证 munmap→close） | 已 RAII |
| `slab_drain` | `std::array<ChildProcess,64>` | 已 RAII |
| `spawn_child` | `UniqueFd` ×6 + pid | 已 RAII（VictimPipes 段） |

**发现**：无新增所有权泄漏或提前释放；早退点行为与 C 基线一致——正常 `return` 关闭本进程 fd，`pr_error`/`exit` 交由内核回收，dirty 页与 quarantine 依赖进程退出而非析构释放。

## 4. 结论

- CPP12 计划条目“验证每个早退点的析构顺序和日志”按上表审计完成；未发现需要修复的路径。
- `spawn_child` 在 `pipe()` 失败时未接管已创建 fd 的窗口与 C 版一致（失败即返回，进程随后退出），保留原语义。
- 后续若引入 `ExploitSession` 级 cleanup，应保持“dirty 资源不因作用域退出而关闭”的边界。
