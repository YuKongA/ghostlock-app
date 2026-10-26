# 43499 JNI v5：资源句柄面（rev6，2026-10-25）

> **状态**：设计稿（L 级）。rev5 评审 7 项阻断，本稿按物理边界重写；**不进入 J1，不改 CMake**。
> **权威**：`adr/0021-jni-abi-and-ownership.md`（ID≠pointer、registry、inFlight/borrow/transfer/
> quarantine、fork epoch、PI 窗口零 registry）＋`jni-actual-attack-contract.md` §1–§4。
> **取代**：rev3/rev4/rev5 全文；`43499-kotlin-logic-rewrite-plan.md` §1.4 与 §三。
> **本轮修订（rev8）**：victim **实现**迁 `common` 静态库（第 2 消费者＝64560 计划），registry 仍属
> 消费方 DSO；`execute_finish_script` **不校验哈希**（用户 2026-10-25 明确强制，记录见 §十三），
> 但保留 open-once 同 fd 执行以消除路径替换。
> **基线**：`bb27b48a`（2.0.8）。G0 未过不跑门禁；本轮不执行 `git add -A`、不链 core。

## 一、相对 rev5 的变更

placement 留 43499（KS 算法进 common）；Page 12 类资源/8 状态；
`install_payload` → `land_page`（提交点四段 + 四类失败）；actor 单入口 + 物理 tag + 最终地址；
victim **实现在 common、registry 留 43499**；finish **不校验哈希**（强制）+ 同 fd 执行；
registry 按 ADR-0021 写全。

## 二、边界与判据

```text
general DSO   facts / process bootstrap（应用级）
43499 DSO     page（placement + landing）/ actor（一次物理原语）/ victim registry
common STATIC KernelSnitch 算法、victim 管道协议与 perf task 泄漏、通用 fd/pid helper
```

**Native 禁止**：`RouteKind`、`select_stack/tcp_zerocopy/multicast_waiter route`、route catalog、
supported、fallback、W1/W2/W3、repair/credential。
**Native 允许**：`PSELECT` / `TCP_ZEROCOPY_RECEIVE` / `MCAST_SENDMSG` / futex PI / socket / fd。

**移植性判据**：把这段 C++ 放到另一个 backend，是否仍是完全相同的物理动作？是 → 可留；
若它知道 43499 的 W1/W2/W3/route/credential/repair/profile → 迁 Kotlin；必须连续执行否则
线程/内核引用失控 → 留在一个 transaction；只为决定下一步 → 迁 Kotlin。

跨层：general 与 43499 **不互传对方的 handle/fd**；Kotlin 可持有各 DSO 的 opaque registry ID；
跨 DSO 协调只经 Kotlin 复制的明列 DTO 值。

## 三、资源与所有权

| 资源 | DSO | 持有 | 终结 |
|---|---|---|---|
| `Page` | 43499 | 下表的 12 类物理资源 | 见 §三.2 |
| `Actor` | 43499 | PI futex、三线程、arm 状态 | execute/cancel 后 close；borrow Page |
| `Victim` | 43499（实现 common） | protocol child（pid + 2 fd + task） | destroy：SIGKILL+waitpid+关 fd |
| `RootedVictim` | 43499（实现 common） | handoff 后的 root child（新 kind） | destroy / 脚本结束 |

### 3.1 Page 内的物理资源（`support.cpp` 实测）

| 资源 | 数量/来源 | 创建 | 消费/终结 | 现位置 |
|---|---|---|---|---|
| `prepare` children | `8 × mm_objs_per_slab` | `clone_child` | **landing 结束**才 kill | `:750`、`:1029` |
| `prepare` memfd | 同上 | `open("/proc/<pid>/mem")` | **landing 结束**才 close | `:751`、`:1027` |
| `spray` children | `(1+MM_PARTIALS) × mm_objs_per_slab` | `clone_child` | 开 fd 后**立即** kill | `:755`、`:819` |
| `spray` memfd | 同上 | `open_memfd` | landing 中按 slab 步长 close | `:756`、`:999` |
| `pre` children/memfd | `mm_objs_per_slab-1` | `clone_child`+`open_memfd` | 开 fd 后 kill；landing 关 fd | `:797-805`、`:991` |
| `post` children/memfd | `mm_objs_per_slab` | 同上 | 开 fd 后 kill；landing 关前 n-1 个 | `:801-809`、`:995` |
| `leak` child | 1 | `clone_leak_child`（**定位 fork**） | `waitpid` 收尸 | `:799`、`:831` |
| `leak` memfd | 1 | `open_memfd(leak child)` | landing 里 `close`（真正放 mm） | `:807`、`:1012` |
| KS 共享映射 | 1 | `glk_ks_dev_create` | placement 返回前 `destroy` | `:778` |
| reclaim socketpair | 2 fd | `socketpair` + `SO_SNDBUF` | Page 终结时关 | `:964`、`:967` |
| shaping socketpair | 2 fd | `socketpair` + 空 `sendmsg` | landing 内关 | `:973`、`:1003-1004` |
| skb 缓冲 | `skb_send_size` | `make_unique<unsigned char[]>` | Page 终结时释放 | `:746` |

### 3.2 Page 状态与 close

`ALLOCATED → PLACEMENT_READY → PREPARED → LANDING → LANDED → CONSUMED`；
旁支 `QUARANTINED`、`CLOSED`。

| 状态 | close 做什么 | 备注 |
|---|---|---|
| ALLOCATED | 释放 skb | 无 pid/fd |
| PLACEMENT_READY | kill prepare children、close 全部 ctx/leak fd、释放 skb | 泄漏候选整组回收 |
| PREPARED | 同 + close reclaim pair | |
| LANDING | **不释放**，转 QUARANTINED | 不可逆段 |
| LANDED / CONSUMED | close reclaim pair、释放 skb（ctx fd 已在 landing 关完） | |
| QUARANTINED | 不解映射、不关 fd、不交给 allocator，钉到进程退出 | |
| CLOSED | 幂等；ID 已 STALE | |

### 3.3 victim：实现在 common，所有权在 43499

| 归 `common/victim` | 归 43499 |
|---|---|
| pipe×3/fork/setpgid/PR_SET_NAME；子进程命令循环（5 种通用探针：getuid / syscall errno / 读 comm / park / handoff）；`perf_find_task`（带 `dm_base/dm_end`）；CLOEXEC 清扫、`/proc/self/fd/N` exec、waitpid | 协议字节与命令表的绑定（C/F/M/P/G）；探针结果的业务判定；registry slot/ID/kind 转移；handoff 后的 root 策略 |

`victim_spec { comm[16], dm_base, dm_end, protocol_table[] }`，**不含 43499 类型/profile/route/W1–W3**；
命令表在 `create` 时深拷贝，运行中不可改（子进程不是 PI 窗口，静态表可接受）。

**为什么不是 general**：general 服务要跨 DSO 协调 ID；此处共享的只是实现，静态库即可。
manifest 记 `common/victim (1 consumer + 1 planned)`；64560 未启动也不构成过度抽象。

## 四、入口

### 4.1 general（应用级，仅 Kotlin）

```c
int32_t glk_general_query_facts(glk_facts *out);
int32_t glk_general_bootstrap_process(const glk_bootstrap *p);   /* 幂等，仅 rlimit */
```

`glk_facts` 只回原始事实：`release/page_size/cpu_online/rlimit_*/syscall_exists[]/
kernel_option[]/socket_option_probe[]`。**禁止** supported/route/SoC/offset/地址/profile 推导。

### 4.2 43499：page（五步）

```c
uint64_t glk43499_allocate_page(const glk_spray_params *p, glk_call_result *out);
int32_t  glk43499_prepare_placement(uint64_t page, const glk_scan_params *p,
                                    glk_placement_result *out);   /* 出 mm_struct */
int32_t  glk43499_prepare_page(uint64_t page, const glk_page_geometry *g,
                                glk_call_result *out);
int32_t  glk43499_land_page(uint64_t page, const glk_payload *p, glk_land_result *out);
void     glk43499_close_page(uint64_t page, glk_call_result *out);
```

- `allocate_page`：建资源壳 + skb；按 §3.1 顺序建 children/fd（pre/post 夹住 leak child）。
- `prepare_placement`：在定位点 fork leak child → child 内跑 KS 扫描 → parent wait/read →
  保留 leak memfd；返回 `mm_struct`（复制值）。KS 共享映射在返回前销毁。
- `prepare_page`：Kotlin 算好 `base` + 几何 → 建 reclaim/shaping socket，校验 base 对齐/范围。
- `land_page`：见 §五。
- `close_page`：见 §三.2。

### 4.3 43499：actor

```c
uint64_t glk43499_prepare_actor(uint64_t page, const glk_actor_setup *p, glk_call_result *out);
int32_t  glk43499_execute_actor(uint64_t actor, const glk_primitive *p, glk_call_result *out);
int32_t  glk43499_cancel_actor(uint64_t actor, glk_call_result *out);
void     glk43499_close_actor(uint64_t actor, glk_call_result *out);
```

`glk_actor_setup`：`cpu / park_timeout_us / thread_count(=3) / priority`。
`glk_primitive`：`tag ∈ {PSELECT, TCP_ZEROCOPY_RECEIVE, MCAST_SENDMSG}` + 该机制的物理参数，
**全部是最终地址**：`final_waiter_addr / final_task_addr / final_lock_addr`、
`buffer_size / nfds / timeout_us / arm_sequence / arm_hold / sockaddr_family_offset / zc_map_len`。
Native 只校验域、对齐、溢出、与 Page 的归属；**不再用 offset+base 重算地址**。

### 4.4 43499：victim

```c
uint64_t glk43499_create_victim(const glk_victim_request *p, glk_call_result *out);
int32_t  glk43499_command_victim(uint64_t victim, uint8_t protocol_code, glk_call_result *out);
uint64_t glk43499_handoff_victim(uint64_t victim, glk_call_result *out);
int32_t  glk43499_execute_finish_script(uint64_t rooted, const glk_finish *p,
                                        glk_call_result *out);
void     glk43499_destroy_victim(uint64_t id, glk_call_result *out);
```

- `handoff_victim` 只做 kind 转移（锁内完成，旧 ID STALE），不带脚本。
- `glk_finish`：`path` + `timeout`，**不校验哈希**（用户强制，见 §十三）。
  Native 仍做**路径→对象**闭环：`openat(O_RDONLY|O_NOFOLLOW)` → `fstat`（regular/owner/mode）
  → 通过 `/proc/self/fd/N` 执行**同一个 fd**。这只消除"检查后被换路径"，不认证内容；
  脚本内容、完整性、权限、SELinux、marker、解释全部由 Kotlin 负责。
- 以上五个入口是 **43499 的 JNI 面**；实现委托 `common/victim`。`perf_find_task`
  （`attack.cpp:444`）迁 `common/perf`，以 `{dm_base, dm_end}` 为参数；43499 不再保留副本。

## 五、`land_page` 是 landing transaction

```text
P0 preflight   校验 bytes/metadata/geometry/socket；不改任何内核可见资源
P1 commit      关闭第一个 memfd（或第一个不可逆 sendmsg）= 提交点
P2 irreversible 关剩余 ctx/leak fd → shaping → N 次 reclaim sendmsg
P3 receipt     报告实际 facts：closed_counts / sendmsg_counts / base / 校验结果
```

| 失败位置 | effect | Page 状态 | 可否重试 |
|---|---|---|---|
| P0 任一校验 | `NOT_APPLIED` | PREPARED | 可（同 Page） |
| P1/P2 且清理全部可证 | `CLEAN_FAILURE` | CONSUMED | **否**（换 Page） |
| P1/P2 中途，引用状态不明 | `INDETERMINATE` | QUARANTINED | 否 |
| 可能有异步引用 | `DIRTY` | QUARANTINED（闭包） | 否，fail-stop |

**"页接受判据"的准确表述**：Kotlin 负责期望条件、payload bytes 与结果解释；
Native 负责报告实际 `sendmsg/reclaim/landing` facts（含实际 base 与计数）；
**Kotlin 不能仅凭输入宣布物理落页成功**。

## 六、actor 窗口纪律

`execute_actor` 必须满足（作为反汇编门禁）：

Kotlin 选 primitive；无 AUTO/默认/supported/fallback；不据地址推断；tag 只在窗口前解析一次；
参数深拷贝到固定结构；窗口内不读 tag、无 switch；worker 只调已选定的直接物理函数；
结果只报物理 facts/effect。

`prepare_actor` 建三线程并 park；`execute_actor` = `plant → arm → disarm → join`；
PI 窗口内零 JNI、零 registry、零分配、零打印、零间接分派。

## 七、registry 协议

```text
lock:  lookup → scope/kind/generation/epoch/state 校验 → inFlight++
unlock: 执行物理操作
lock:  inFlight-- → 发布 effect/state
```

- 普通锁竞争**不返回 BUSY**；BUSY 只用于业务上不允许并发的情形（见下）。
- `close`：`OPEN→CLOSING`（锁内）→ 锁外等 `inFlight==0` → 锁内释放一次 → `generation++`。
- `close_page` 遇到 persistent borrow（Actor 还借 Page）→ `BUSY`，**不在锁内等 borrow**。
- 保留：generation 回绕→永久退役 slot；scope owner；`JNI_OnUnload` 仅 clean；
  dirty/unknown/inFlight/borrowed 钉住；atfork prepare/parent/child；CLOEXEC/fd allowlist；
  close×close 恰一次；execute×close 等待；prepareActor×closePage= BUSY。

## 八、KernelSnitch 与 placement

`clone_leak_child` 在 spray 之后、pre/post 之前 fork（`support.cpp:797-801`），child 内跑 KS
（`:511`），parent `waitpid`（`:831`）后保留该 child 的 `/proc/pid/mem` fd（`:807`），
landing 才 close（`:1012`）⇒ 扫描身份绑定**这个定位 child 的 mm**，不是任意 dead mm；
在 JVM 线程另起 general 扫描无法命中。

因此 KS 算法进 `common/kernelsnitch`（C11 静态库），placement 留 43499；`ks_bridge.c` 撤销。
KS 生命周期必须先是 `stop(volatile) → wake → join(保存 tid) → munmap`
（`kernelsnitch.h:141-146` detached、`:124` futex wait、`:606-616` munmap）。

判据：placement 的 `mm_struct` 落在本进程喷射区间；landing facts 与 legacy `land_with` 一致；
KS `open→cancel→close` ×100 后线程数回基线。

## 九、Kotlin 权威

| Kotlin | Native |
|---|---|
| profile、SoC、offset、最终地址（含 waiter/task/lock） | 域/对齐/溢出/归属校验 |
| 生成 final bytes + metadata + 页接受期望 | 复制字节、结构校验、报告 landing facts |
| 选 primitive、重试/fallback/settle、结果解释 | 执行一次不可拆 actor 事务 |
| victim 步骤、脚本内容与完整性、root/SELinux/marker | 协议字节、同 fd 执行与 fstat 检查 |
| 资源编排（ID 交给谁、何时 close） | registry + inFlight/borrow |

## 十、删除/迁移

巨型 handle、raw pointer、`page[2]` 槽、`profile/addresses/runtime` 入 handle、
`mode/leaf/route/fast_repair/attempts`、`g_exploit_session`、`ApiState`、`attack::exploit_t0`、
`ks_bridge.c`、`lastStep/lastErrno`、`runMulticast`、`landWithPayload`、`skbPayloadSize`。
迁移：KS → `common/kernelsnitch`；victim_process + perf_find_task → `common/victim`、`common/perf`。

**文件级标记**见 `43499-migration-markers.md`：13 个文件已 `.disabled`，保留 TU 符号自闭合
（`nm` 交集为空）；另有 route class 的每调用状态待拆（marker §七）；只剩 4 处内嵌逻辑。

## 十一、G0：不改 CMake

根 CMake 是 `LANGUAGES C ASM`，`core/*.cpp` 需 C++ 工具链；当前 core 仍含
`g_exploit_session/ApiState/route controller/fallback/ks_bridge`，整体链入会把要删的层带回
生产 DSO，且无 manifest → 无法做 include/link firewall。

现在只做：冻结 `bb27b48a` + hash；单 writer；迁移/43284/UI Finish/JNI 分批；
写精确 manifest；写逐资源所有权表；修订 v4/ADR/UML/legacy guide 的取代关系。

manifest（逐文件加入，禁 glob）：`registry / result_abi / page_resource / placement / landing /
actor / primitive_pselect / primitive_tcp_zerocopy / primitive_multicast / victim_resource /
jni_bridge` + 静态库 `common/kernelsnitch`、`common/victim`、`common/perf`。
每 TU 禁依赖 old session / profile / route controller / ks_bridge / legacy 聚合头。
现有 `core/*.cpp` 只作只读迁移源或独立 dev reference target。

## 十二、批次

| 批 | 内容 | 门禁 |
|---|---|---|
| **G0** | §十一 | 基线可复现、manifest 可审 |
| **J1** | registry + result ABI + jni bridge 骨架 | ADR 判据：0/1/MAX、wrap、wrong kind、stale epoch |
| **J2** | page：allocate/placement/prepare/land/close | 真机：placement 落点 + landing facts A/B |
| **J3** | actor + primitive + 窗口纪律 | 反汇编（窗口内无 switch/BLR/tag 读取）+ W1 |
| **J4** | victim：common 实现 + 43499 registry（协议/handoff/finish） | `uid` 前≠0 后=0；O_NOFOLLOW/同 fd 负例 |
| **J5** | W3 + 端到端 + `cmp_disasm` 重冻结 | `enforce_after=0`、`uid=0`、无可写全局 |

## 十三、风险与沿革

| # | 风险 | 处置 |
|---|---|---|
| R1 | placement 与 landing 的可分离性 | 真机记录落点/facts；不通过则合并两段 |
| R2 | registry 进 PI 窗口 | 编译期开关 + 窗口符号引用=0 |
| R3 | JVM 内 fork/clone 与 GC | `:exploit` 进程门禁，20 次冷机 |
| R4 | landing 的 INDETERMINATE 不可回收 | quarantine 钉住，容量 fail-closed |
| R5 | 基线混杂 | G0 未过不跑门禁 |

**沿革**：

- victim 归属三改（43499→general→43499→common）：共享的是**实现**不是**所有权**；
  ID 按 ADR-0021 留消费方 DSO，JNI 面仍是 `glk43499_*`。
- **强制确认（2026-10-25 · 用户）**：finish **不校验内容哈希**。依据 §4.4/评审 B6；
  代价＝同一 inode 在检查后被写入则执行新内容（路径替换已由 open-once +
  `/proc/self/fd/N` 消除）。缓解＝脚本只在 app 私有目录、Kotlin 独占写权限。
  债务 `D-FINISH-HASH-01`；偿还＝出现非私有路径/跨进程可写/下次 Finish 改动时重评。
