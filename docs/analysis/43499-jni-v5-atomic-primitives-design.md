# 43499 JNI v5：资源句柄面（rev4，2026-10-25）

> **状态**：设计稿（L 级）。rev3 **已被否决**（8 项阻断），本稿逐条回应，**不进入 J1**。
> **权威（收敛后）**：`docs/analysis/adr/0021-jni-abi-and-ownership.md`（Accepted）——
> ID 不是 pointer、每 DSO 固定 registry、inFlight/borrow/transfer/quarantine、
> fork epoch、PI 窗口内零 registry 访问；`jni-actual-attack-contract.md` §1–§4。
> **本稿取代**：rev3 全文；`43499-kotlin-logic-rewrite-plan.md` §1.4 与 §三。
> **与 v4 的关系**：wire 生成器、registry 规则沿用 `43499-43284-jni-rewrite-plan.md` v4，
> 本稿只改 43499 的资源面；ADR-0021 不改；UML（组件/状态/序列）在批准后**同批**更新。
> **基线**：HEAD `be0f2737`。工作树有 289 个非 build 改动、多线并行，
> 见 §十二 G0——**基线未隔离前任何门禁都不可归因**。

## 一、总判与撤回

rev3 唯一可保留的一句：**状态属于具体资源句柄，Kotlin 持有 ID 并决定下一步调用谁**。
其余全部撤回：raw pointer、无 registry、echo 同一 h、巨型 session、
profile/addresses/runtime 进 handle、route/mode/leaf/fast_repair、discovery 并入 prepare、
Finish 并入 backend、重复 close 可崩溃、失败默认可重试。

本稿改为 **ADR-0021 已冻结的资源模型**，不再新造 ABI。

## 二、逐条回应（按审查章节）

| # | 审查意见 | 本稿处置 | 位置 |
|---|---|---|---|
| 一 | raw-pointer `jlong` 无法防 UAF | `jlong` = `slot|generation|epoch` 的 **ID**；每 DSO 固定 registry；解引用前先查表 | §六 |
| 二 | echo 同一个 h 无所有权语义 | 普通操作返回 result；factory 返回新 ID；transfer 返回新 kind + 旧 ID STALE；close 等 inFlight 归零后释放一次 | §六 |
| 三 | 巨型 `glk43499_handle` = 换名 session | 拆成 Discovery / Page / Actor / Victim / RootedVictim 五种小资源；各自独立 close | §四、§五 |
| 四 | Kotlin 权威被搬回 Native | Native 收 `finalAddress` + `finalPayloadBytes` + 中性 metadata + 物理 mechanism 参数；无 profile/route/mode/leaf/fast_repair/attempts 语义 | §五、§十 |
| 五 | discovery 并入 prepare；KernelSnitch 归属错误 | discovery 独立到 general DSO；43499 不链接/include KernelSnitch、不持 cursor；只收 copied facts。KernelSnitch 必须先修 stop→wake→join→munmap | §八、§九 |
| 六 | `prepareProcess` 混三种生命周期 | 拆成 `queryProcess`（只读 DTO）/ `bootstrapProcess`（幂等进程级）/ Actor 自带 affinity；stdio 移出 43499 | §五 |
| 七 | 失败不总能回滚重试 | 结果带 `cleanliness ∈ CLEAN/CANCELLED_CLEAN/DIRTY/INDETERMINATE`；只有前两者允许重试 | §七 |
| 八 | 双槽两套 owner | 不存在槽：每页一个独立 PageHandle，Kotlin 决定哪个 ID 给谁；Native 无 CURRENT/PREBUILT 名字 | §四 |
| 九 | Finish/PARK/HANDOFF 并回 backend | Victim 只做 `create/command/handoff/destroy`；finish 脚本是 `executeFinishScript(rooted, path, hash)`；策略在 Kotlin | §五 |
| 十 | 工作树不可作基线 | G0：单 writer + 基线快照 + 目录迁移/43284/UI finish 分批隔离；CMake 未链 `core/*.cpp` 前不跑任何 43499 门禁 | §十二 |

## 三、三层边界

```text
general DSO（仅 Kotlin 调用）   discovery：KernelSnitch 扫描 → copied facts
43499 DSO                        page + actor：物理页与 PI 事务
43499 DSO（victim 资源组）       protocol victim → handoff → rooted
```

- backend 之间、backend↔general **不互调**（ADR-0021 §1）。
- 跨层只走 **值**：general 的 `{found, mmStruct}` 经 Kotlin 变成 43499 的入参；
  不传句柄、不传指针、不传 fd。
- 43499 不再 include `kernelsnitch/`，也不再需要 `ks_bridge.c`（J4 删除）。

## 四、资源与所有权

| 资源 | DSO | 代表什么 | 终结 | 借用关系 |
|---|---|---|---|---|
| `Discovery` | general | 一次 KernelSnitch 会话（共享映射 + 工作线程） | close：stop→wake→join→munmap | 无 |
| `Page` | 43499 | 一次 mm 喷射（死 mm 的 `/proc/pid/mem` fd 集合） | close：杀子进程/关 fd | Actor 借用 |
| `Actor` | 43499 | 一次 PI 事务（futex、三线程、arm 状态） | execute 或 cancel 后 close | **persistent borrow Page** |
| `Victim` | 43499 | protocol child（pid + 2 fd + task） | destroy：SIGKILL+waitpid+关 fd | 无 |
| `RootedVictim` | 43499 | handoff 后的 root child（新 kind，旧 ID STALE） | destroy / script 结束 | 无 |

`Page` 与 `Actor` 是可独立关闭的资源：Kotlin 可以丢掉一个 Page 而保留另一个
（legacy 的 prebuilt 只是"另一个 PageHandle"，不再是 native 内部槽位）。

## 五、入口（按 DSO 分组）

### 5.1 general（仅 Kotlin 调用）

```c
int32_t  glk_general_query_target(glk_facts *out);              /* 只读 DTO */
uint64_t glk_general_open_discovery(const glk_discovery_params *p, int32_t *code);
int32_t  glk_general_scan_discovery(uint64_t id, const glk_scan_params *p,
                                    glk_discovery_result *out);
int32_t  glk_general_cancel_discovery(uint64_t id, glk_call_result *out);
void     glk_general_close_discovery(uint64_t id, glk_call_result *out);
```

`glk_discovery_result` 是**复制出来的值**：`found`、`mm_struct`、`probes`、`rounds`、`errno`。
DLL 内不保留任何 cursor/指针；调用方拿不到映射地址。

### 5.2 43499：page

```c
uint64_t glk43499_allocate_page(const glk_spray_params *p, glk_call_result *out);
int32_t  glk43499_install_payload(uint64_t page, const glk_payload *p,
                                   glk_call_result *out);
void     glk43499_close_page(uint64_t page, glk_call_result *out);
```

- `allocate_page`：只做 mm 喷射（死 mm + fd），**不做泄漏**（discovery 在 general）。
- `install_payload`：`{base, finalBytes, metadata}` → 校验 → 关 fd 释放 → socketpair 回收
  → 逐字节拷入 → 建立 `Page.state = INSTALLED`。只允许一次。
- `glk_payload.metadata`（中性，无业务含义）：
  `abi_version / length / required_alignment / layout_tag / range_count /
  target_offset / write_width / stored_value` + `range_count × {offset,len}`。
  Native 只校验：长度、对齐、range 在界内、`target_offset+write_width ≤ length`、
  该偏移的字节解码后等于 `stored_value`。**不解释** Credential/W1/W2/W3。

### 5.3 43499：actor

```c
uint64_t glk43499_prepare_actor(uint64_t page, const glk_actor_setup *p, glk_call_result *out);
int32_t  glk43499_execute_actor(uint64_t actor, const glk_mechanism *m, glk_call_result *out);
int32_t  glk43499_cancel_actor(uint64_t actor, glk_call_result *out);
void     glk43499_close_actor(uint64_t actor, glk_call_result *out);
```

- `prepare_actor`：登记 persistent borrow（Page），按 `setup.cpu` 绑核、建三线程并 park。
- `execute_actor`：plant → arm → disarm → join，一次调用一笔事务；返回
  `status/step/errno/effect/cleanliness/attempts/calls/successes/userspace_clean/kernel_disarmed`。
- `glk_mechanism` 是**物理机制**的 typed union（multicast/tcp/select）：
  waiter/task/lock offset、buffer size、sockaddr family offset、nfds、arm sequence/hold、
  timeout。没有 fallback 列表——一次调用只跑 Kotlin 指定的那一个。

### 5.4 43499：victim 资源组

```c
uint64_t glk43499_create_protocol_victim(const glk_victim_request *p, glk_call_result *out);
int32_t  glk43499_command_victim(uint64_t victim, int32_t cmd, glk_call_result *out);
uint64_t glk43499_handoff_victim(uint64_t victim, const glk_handoff *p, glk_call_result *out);
void     glk43499_destroy_victim(uint64_t id, glk_call_result *out);
int32_t  glk43499_execute_finish_script(uint64_t rooted, const glk_finish *p,
                                        glk_call_result *out);
```

`handoff_victim` 在 registry 内**原子**完成 `Victim(OPEN) → STALE` 与
`RootedVictim(OPEN)` 的替换；旧 ID 立即失效。PARK 是 `command_victim` 的一个命令，
句柄保持 OPEN。脚本内容/权限/SELinux 恢复/marker 校验由 Kotlin 负责；
native 只传同一 fd 并触发、校验 `expected_hash`。

### 5.5 进程级

```c
int32_t glk43499_query_process(glk_process_facts *out);        /* 只读：rlimit/cpu/release/selinux */
int32_t glk43499_bootstrap_process(const glk_bootstrap *p);    /* 幂等；只改 rlimit */
```

affinity 不进 bootstrap：它是 `Actor.setup.cpu` 的调用内 scoped 设置，执行完恢复。
stdio 不进 43499（日志由 Kotlin/上层决定）。

## 六、句柄协议（照抄 ADR-0021，不再自创）

- **ID 编码**：`slot(12b) | generation(20b) | epoch(16b) | dso(8b) | kind(8b)`；
  任何一项不符 → `STALE`。每 DSO 一张固定容量表（容量与半满阈值见 ADR CBN）。
- **并发**：入口先 try-lock slot；占用 → `BUSY`。**PI 窗口内不访问 registry**
  （`execute_actor` 在进入窗口前把全部参数与指针取到栈上）。
- **borrow**：`Actor → Page` 是 persistent borrow；只有 CLEAN 结束才解除；
  dirty 时整条依赖闭包一起 quarantine。
- **close**：`OPEN → CLOSING → 等 inFlight=0 且 borrow=0 → 释放一次 → generation++`。
  旧 ID 再调用一律 `STALE`，**永不崩溃**。
- **transfer**：`handoff_victim` 在锁内完成 kind 替换，旧 ID STALE。
- **fork**：atfork child 重建 mutex、epoch++、清空 slots；pre-fork ID 全 STALE。
- **quarantine**：dirty/unknown 的 slot 不移交 allocator，钉到进程退出；容量耗尽 fail-closed。
- **不保存业务状态**：registry 只有 `kind/state/generation/资源指针/dirty 原因`；
  不知道 route、不知道 W1/W2/W3、不决定下一步。

## 七、失败分类

| 结果 | 含义 | 允许重试 | 资源处置 |
|---|---|---|---|
| `CLEAN / NOT_APPLIED` | 确定未写入内核 | 是 | 本次资源已终结 |
| `CANCELLED_CLEAN` | 调用方取消，确定未写 | 是（按定义） | 已终结 |
| `DIRTY` | 用户态脏或内核未 disarm | **否** | 整条闭包 quarantine，fail-stop |
| `INDETERMINATE` | 无法证明是否写入 | **否** | quarantine + fail-stop |

`execute_actor` 是唯一能产生 DIRTY/INDETERMINATE 的入口；其余入口只有前两类。
"失败即可重试"从规则里删除。

## 八、discovery 与 page 的物理顺序（可分离性证据）

必须按 `allocate_page → general.scan → install_payload` 三段走，依据：

1. 喷射产生的是**死 mm + fd**：`clone_memfd` = fork → `open("/proc/<pid>/mem")` →
   `kill_child`（`support.cpp:575-580`）；fd 关闭才释放 mm 与页（`:991-1028`）。
2. 泄漏必须发生在"死 mm 还在"的时候；回收必须在泄漏之后。
3. general 的扫描只需要"系统里存在这些候选 mm"这个事实，不需要 43499 的任何资源
   ⇒ 跨层只传 `mm_struct` **值**，不传 fd/指针。

门禁：真机记录 `mm_struct` 落在本进程喷射区间内，且 `install_payload` 的页接受
结果与 legacy `land_with` 一致（同一 profile、同一轮、A/B 各 1 次）。

## 九、KernelSnitch 前置修复（阻断 J4）

现状（实证）：`__increase` 用 `pthread_create` + `pthread_detach`
（`glgeneral/kernelsnitch/kernelsnitch.h:141-146`），线程阻塞在
`ks->inc_futex[id]` 的 `FUTEX_WAIT_PRIVATE`（`:124`）；`kernelsnitch_cleanup`
直接 `munmap` 共享区（`:606-616`，含 `munmap(ks, ...)`）。残留线程仍在
已解除映射的 futex 上等待 → 复用地址/唤醒后可访问失效内存。

迁入 general JNI **之前**必须改成：

```text
stop(置 volatile stop) → wake(FUTEX_WAKE 每个 inc_futex[id]) → join(保存 tid) → munmap
```

判据：
- `open→cancel→close` 100 次循环，`/proc/self/task` 线程数回到基线；
- cancel 与 close 并发 ×100，无泄漏线程、无 SIGSEGV；
- ASan/hwasan（host 或 AVD）下 `munmap` 后无残留访问；
- 修复前，general discovery 不得接 JNI，43499 也不得调用它。

## 十、Kotlin 权威清单

| Kotlin 负责 | Native 只做 |
|---|---|
| profile 解析、地址计算（含 image→direct-map 别名） | 地址域/对齐/溢出校验 |
| 生成 **final payload bytes** + metadata + 页接受判据 | 复制字节、结构校验、声明区间校验 |
| 选择物理 mechanism、重试/fallback、settle、验证判定 | 执行一次不可拆 actor 事务 |
| victim 脚本、root 管理、SELinux 恢复、marker | 同一 fd 触发 + hash 校验 |
| 资源编排（哪个 ID 交给谁、何时 close） | 资源 registry + inFlight/borrow |

## 十一、撤回/删除清单

`glk43499_handle` 巨型对象、raw pointer ID、`stage/spray/page[2]` 槽位、
`profile/addresses/runtime` 入 handle、`mode/leaf/route/fast_repair/attempts` 语义、
`prepareProcess` 三合一、`g_exploit_session`、`ApiState`、`attack::exploit_t0`、
`ks_bridge.c`、`lastStep/lastErrno`、`runMulticast`、`landWithPayload`、`skbPayloadSize`。

## 十二、G0 基线隔离（先于 J1）

现状：`git status` 有 **289** 个非 build 改动，混有 `glk*→gl*` 目录迁移、
`common/general` 角色对调、43284 大范围替换、`legacy/` 删除、UI finish 删除、
多份 plan/ADR/UML 同时修改；`CMakeLists.txt` 的 `ghostlock43499` 仍只列
`jni43499.c`，未链 `core/*.cpp`。任何门禁失败都无法归因。

G0 出关条件（每条都需用户确认）：

1. 当前树打成带标签的快照（不 push），作为唯一可归因基线；
2. 目录迁移、43284、UI finish 各自单独批次/提交，不与 JNI 设计同批；
3. `CMakeLists.txt` 能构建出"包含 core 实现"的 `libghostlock43499.so`；
4. 单 writer：同一时刻只有一条写入流。

## 十三、批次（J1 之前的门）

| 批 | 内容 | 门禁 |
|---|---|---|
| **G0** | §十二 四项 | 基线可复现；`nm` 能定位每个符号的来处 |
| **J1** | general `query/…` + 43499 `query_process/bootstrap_process` + ID registry 骨架 + receipt/wire 生成器（含 ADR 判据：0/1/MAX、wrap、wrong kind、stale epoch） | host 协议矩阵；无业务符号 |
| **J2** | `allocate_page/install_payload/close_page`（payload 由 Kotlin 给） | 真机：页接受 A/B、字节级一致 |
| **J3** | `prepare_actor/execute_actor/cancel/close` + borrow/quarantine | 真机 W1；execute×cancel/close 负例 |
| **J4** | general discovery（含 §九 修复） | §九 判据 + 真机扫描 |
| **J5** | victim 资源组 + handoff kind 转移 | `uid` 前≠0 后=0；旧 ID STALE |
| **J6** | W3 + finish script + `cmp_disasm` 重冻结 | `enforce_after=0`、`uid=0`、无可写全局 |

## 十四、风险

| # | 风险 | 处置 |
|---|---|---|
| R1 | general 扫描与 43499 喷射的时序（§八） | 真机 A/B，记录 leaked 落点 |
| R2 | registry 在 PI 窗口被访问 | 编译期开关 + 窗口内符号引用=0 的门禁 |
| R3 | JVM 内 fork/clone 与 GC | APK `:exploit` 进程门禁，20 次冷机统计 |
| R4 | 固定容量 registry 耗尽（dirty 钉住） | 半满告警；超限 fail-closed |
| R5 | 基线混杂导致假红/假绿 | G0 未过不跑门禁 |

**未决**：`execute_finish_script` 的 `expected_hash` 是脚本内容哈希还是产物哈希，
需在 J6 前定；本稿不假设。
