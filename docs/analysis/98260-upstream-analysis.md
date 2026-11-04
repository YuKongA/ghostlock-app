# 上游分析：YuKongA/CVE-2026-98260（timer / POSIX CPU timer UAF）

> 级别：分析。日期 2026-10-28。对象：`https://github.com/YuKongA/CVE-2026-98260`（单 commit `ad1f4e7`，MIT）。
> 证据等级：**【码】**=上游源码/注释直接可见；**【推】**=依通用内核实现推断，需对 6.12.69 源码/上游核对；**【读】**=README 声称，仓库无设备日志。
> 引用：`timer/timer.c:NN` 指上游克隆（只读分析于 `/tmp/CVE-2026-98260`）。

## 1. 机制摘要

1. **对象**：`timer_create(CLOCK_THREAD_CPUTIME_ID)` → `alloc_posix_timer()` 从 `posix_timers_cache` 分配 `struct k_itimer`（req=alloc=`0x100`；分配点硬编码 `do_timer_create+0x94`，`timer/timer.c:67-70`）。posix CPU timer 的内嵌 `timerqueue_node` 位于 `k_itimer+0x78`（`timer/timer.c:1171,1379`）。
2. **UAF 条件【推】**：arm 一个 victim timer 后触发 `execve`，在 exec 的 timer teardown 被 `SIGKILL` 打断的窗口里，`k_itimer` 已释放但 queue node 仍留在目标 task 的 POSIX-CPU-timer 红黑树中。代码只把 `CLD_KILLED` 视为 "UAF armed"（`timer/timer.c:1300-1323`）；"失败即 park、绝不 reap"（`timer/timer.c:28-31`）。
3. **回收/复用【码+推】**：4096 timers 撑满 order-1 slab（8 KiB / 32 objects；由 `SOCKBUF=8192`、`pfn&~1`、`&~0x1fff` 反推，**slab order 未直接探测**）；victim slab 整页归还 buddy 后，被 **8 KiB AF_UNIX SOCK_STREAM 写** 复用的 `kmalloc-8192` skb data 抢占并长期持有（2048 socketpair，约 750 MiB 峰值）。
4. **写原语【推】**：reap zombie 时 `timerqueue_del→rb_erase` 把 payload 当节点：`node+0x00=__rb_parent_color`（写值）、`node+0x10=rb_left`（写地址）⇒ 一次 8 字节任意写；zero 模式 `parent_color=0`，fake 模式 `parent_color=node`（自指，触发 `RB_EMPTY_NODE` WARN，`timer/timer.c:1708-1717`）。
5. **触发时序【码】**：标定基址+预填 4096 timers → fork 子进程 C exec 自身泄漏 cred/task → 每次 plant fork target T（50k VMA + 3 locker + killer + exec_thread 4096 timers）→ trace 观测页 free/alloc → 两阶段喷射 → reap 触发写（`timer/timer.c:769-789,1046-1084,1100-1373`）。

## 2. 两条 endgame

| 链 | 路径 | 成功判据 | 清理 |
|---|---|---|---|
| real-cred 零写 | 一次 0 写 `cred+0x8`（uid+gid）→ C `setresgid(0)`+`setuid(0)`+`exec sh`（`timer/timer.c:1689-1700,1548-1601`） | `uid=0`、`u:r:shell:s0`、`ROOT_SHELL_OK`【读】 | 成功 plant 已 erase 无 stale node；**仅当 `g_held==0` 可正常退出，否则 park**（`timer/timer.c:1770-1774`） |
| fake cred + KSU late-load | fake `struct cred`（full caps/SID=1/静态 root_user 等指针）改写 `task->cred` → 关 SELinux → 修正 uid → 唤醒 C，C fork worker exec `libksud.so late-load`（`timer/timer.c:1701-1735,836-873`）：worker 因 fork 的 `prepare_creds` 归一化获得真实 full-cap cred | `[ksu] … uapi: 6`、`KSU_LOADED`、`su` 为 `u:r:ksu:s0`【读】 | **P 永久 park（持有 fake cred 页）**，重启是唯一清理 |

## 3. 环境前提（决定宿主形态）

- **运行身份**：adb/shell，uid 2000、`u:r:shell:s0`；README 称可**读** tracefs 与 `/proc/slabinfo`（`README.md:17-20`）；代码需 **O_WRONLY 写** tracefs（`timer/timer.c:152-160,181-190`）与写 `/data/local/tmp`（`timer/timer.c:865-872`）⇒ **tracefs 可写性未核实**；`/proc/modules` 判据见 `timer/timer.c:858,861`。
- **tracefs 是硬依赖**：启 `kmem:kmem_cache_alloc/mm_page_free/mm_page_alloc`（`timer/timer.c:181-190`），读 `per_cpu/*/trace_pipe_raw` 泄漏内核指针、反推 KASLR slide 与线性映射基址；无 tracefs 则原语不可用。
- **独立进程语义**：child 以 `execve("/proc/self/exe","wait",mode)` 重入自身（`timer/timer.c:1435-1439`）；target 以 `argv={""}` exec 自身并靠 `argv[0][0]=='\0'` 判 exec 成功（`timer/timer.c:1631-1632,765-766`）。宿主进程内运行会破坏这两条语义。
- **内核前提**：精确 `6.12.69-android16-6-g242e1a07f878-ab15865446-4k`、SM8850/PLZ110、4K 页、SLUB、KASLR on、SELinux Enforcing、KASAN 关闭、`panic_on_warn=0`（fake 链必需）。
- **KernelSU**：已安装 manager（缺省 `me.weishu.kernelsu`），其 `lib/arm64/libksud.so` 支持 `late-load`（uapi 6），`--kmi android16-6.12`；成功判据 `/proc/modules` 含 `kernelsu `（`timer/timer.c:858-861`）。

**结论**：in-process JNI 结构性不可行（域/无 tracefs/无 `/proc/self/exe` 语义/park 生命周期冲突）；唯一形态 = **shell 域启动的独立静态 helper 可执行体**，app 只做编排/日志解析/状态机。

## 4. 参数分类（移植必须的 profile 输入）

| 类 | 内容 | 来源 |
|---|---|---|
| 每构建必重推 | timer 站点/低 21 位、`k_itimer`/cred/task 尺寸与偏移、`selinux_state`/`panic_on_warn`、fake cred 的 4 个静态符号、node 偏移、SELinux blob/SID、slab order（→SOCKBUF） | kallsyms/BTF/disasm（`timer/timer.c:67-104`） |
| 运行时发现（不进口 profile） | page_alloc/page_free event id、KASLR slide、线性映射基址 | tracefs 投票 |
| 调参（**编译期常量，不进口 profile**） | `NTIMERS/VICTIM_IDX/PREFILL_TIMERS/VMA_COUNT/LOCKERS/CPU_TRIG/SPRAY_*`、tracefs 路径 | 上游 `#define` |
| KSU 阶段 | manager 包名、KMI、late-load 参数、ksud 回退路径、uapi 下限 | 上游脚本 `timer/timer.c:836-873` |

## 5. 资源/生命周期/安全（移植必须编码）

- **故意驻留**：fake 成功或任何失败 plant 后 P `park_forever`（`timer/timer.c:813-819`）；2048 socketpair/skb、fake cred 页、不确定状态僵尸必须保持持有。
- **不可逆点**：释放 `timer_go`（`timer/timer.c:1278-1284`）后 UAF 已 armed；此后 kill/timeout/reap 都可能 panic。
- **安全守则**：触发后绝不 kill/timeout/reap；`parking without reap` ⇒ app 进入"必须重启"；同一 boot 最多一次 armed；成功后独立验证（`/proc/modules`+`su -c id`），不把日志当唯一判据。
- **风险**：`panic_on_warn=1` 时 fake ① 必 panic（移植需把 SELinux/panic_on_warn 处理提前到 fake cred 之前）；静态偏移错 ⇒ 写错内核地址；页被系统抢 ⇒ 收尸 panic；socket 预算/阻塞；SELinux 关闭残留。

## 6. 上游诊断工具（移植期核对用）

`tools/trace_calls.c`（kmem 记录布局/站点）、`trace_parse.c`（slide 低 21 位+对齐）、`leak_iso.c`（泄漏通道+VMA 方案）、`timer_trace.c`（timer/kmem 记录）、`fork_trace.c`（task 分配站）、`stream_test.c`（8 KiB AF_UNIX）、`trace_leak_test.c`（单进程全链路）。`timer/timer` = 静态 aarch64 ET_EXEC（无 build-id/无 RELRO，NDK 28/API 35，307192 B）。

## 7. 证据缺口（未验证）

1. "exec 被打断导致漏摘除" 的具体内核路径为推断；需对 6.12.69 源码/woshigenm 上游核对。
2. 上游无真机日志；README 成功表为作者声称。
3. `SELINUX_STATE_STATIC-7` 语义、`SELINUX_BLOB_OFF` 字段名、`k_itimer+0x78` 与 cred 偏移需在目标构建 BTF/pahole 核对。
4. slab order、`wmem_max/rmem_max`、trace buffer 大小、CPU 数未探测（移植 preflight 需补）。
5. 上游仓库无 LICENSE 文件（README 声称 MIT）；发布前需核对。

## 8. 对 GhostLock 的移植结论（供计划使用）

1. **形态**：独立静态 helper（`add_executable`，`lib*.so`），由 **Shizuku UserService 进程内 JNI posix_spawn** 启动；app 只做编排/日志解析/验证/重启状态机，**永不 kill/timeout/reap**。
2. **参数**：`#define` 常量改为运行时 `struct glk98260_params`（argv 十六进制串），profile 提供数值；helper 不内置目标内核常量。
3. **链选择**：fake cred → 内置 KSU late-load 为 backend 目标（GhostLock 的用途）；real-cred 链只作诊断/前置实验（default 模式保留）。
4. **顺序重排**：SELinux 关闭与 panic_on_warn 处理提前到 fake cred 之前（上游顺序有 panic 风险）。
5. **不可宣称**：未过真机门禁前不得标 supported；UI 明示"重启前不可清理"。
