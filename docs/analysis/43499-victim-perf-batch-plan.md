# 43499 拿 root · victim + perf 定位（L 级 · 待认可）

> 沿革：本文件原名"读面批次"，前提是"缺内核读 ⇒ 要有 read_word" ✗ —— **该前提已被证伪**：
> 退役 43499 一次也没有读内存 ✓，它 **spawn 一个受害者子进程** ✓、用 **perf 侧信道**泄漏其 `task_struct` 地址 ✓、
> 再写 `child_task + task_cred_off()` ✓（退役 `steps.cpp` 原文："/\* Spawn one victim and write the credential. */"、
> 注释里点名 "the perf leak" ✓）。⇒ 本批改为**移植 victim + 接 perf** ✓，读面**不需要** ✗。

## 一、目标与判据

- 目标：在设备上得到 `child_task`（受害者的 task_struct 地址 ✓）⇒ `target = child_task + task_cred_off()` ✓
  ⇒ 载荷页的凭据图像写上去 ⇒ **`after uid=` 由 2000 变 0** ✓；
- 判据（唯一）：那一声 `after uid=0 0 0 0` 的**原始输出** ✓（不接受间接说法 ✓）。

## 五、交付与技术债

- 交付：victim 单元 + perf 接线 + w2 目标计算 + 真机 root 归档 ✓；
- 技术债：w3（selinux/seccomp 那两级）本批**不移植** ✓（若 w2 已得 root 即达判据 ✓），需要时另批 ✓。

## 六、移植边界勘定（照旧码逐条，2026-10-11 晚）

- `victim_context.hpp`（17 行 ✓）：**六个管道端 + 子进程 pid** ✓，只依赖一个 fd 包装（`UniqueFd`）⇒ 本树换个等价包装即可 ✓；
  生命周期是**显式**的（注释原文："Scope exit never signals the child … retirement is always explicit
  (`retire_child()`) and handoff transfers the pid away (`release_child()`)"）⇒ 移植时**不得**改成析构里收尾 ✗；
- `victim_process.cpp`（287 行 ✓）：fork + 子侧协议（`child_main`）+ 三条管道：`task_read`（perf 泄漏结果从子侧送回 ✓）、
  `cmd_write`、**`uid_read`**（受害者把**自己的 uid** 送回 ⇒ **这就是"拿到 root"的判据通道** ✓，比我在父进程里打印 `getuid` 更准 ✓）；
- **必须原样带过去的一条**（旧码注释原文）：

  > `rooted exits kfree the static init_cred (w2 stores it with no get_cred). park forever,
  > oom_score_adj -1000 so lmkd skips us.`

  ⇒ 拿到 root 的子进程**必须永久驻留**（不能退出 ✗：退出会 `kfree` 掉静态 `init_cred` ✓）并把 `oom_score_adj` 设为 `-1000` ✓；
  这是**安全相关**的细节 ⇒ 移植时逐字保留 ✓，不许"简化" ✗。
- 依赖面：`platform/runtime.hpp`（SELinux 相关 ✓，本批可先不移植 ✓）+ `primitives.hpp`（写原语 ✓，本树已有等价物 ✓）。
- 由此，本批的**最小可行子集**为：`victim_context` ✓ + `spawn_victim`/`child_main`/`uid_read` 通道 ✓ + park-forever ✓；
  w3（selinux/seccomp）与命令管道的高级用法留到需要时 ✓。

## 十一、第 3 步：接线完成，设备给出具名结果 victim-leak-disagreed（2026-10-11 深夜）

- 接线已进 main（退役顺序）：spawn_victim（带**重试**，退役同形："Retry means the perf leak missed
  and the chain should respawn"）⇒ target = child_task + abi.task_struct.cred（1944，按映射表读）⇒
  载荷页 target 同源 ⇒ 跑 w1/w2/w3 ⇒ victim_report_uid ⇒ **判据 uid==0** ⇒ 拿到就 victim_park（驻留），
  没拿到/失败就 retire_child（不留半个）；
- 失败名分开：victim-pipes-failed / victim-fork-failed / victim-leak-disagreed（三次重试后）；
- **设备实测**：main: step result w1 => victim-leak-disagreed ⇒ 9 次投票里没有两次一致；
  而单次投票的探针 perf_ops_probe 是 PASS ⇒ 结论是**投票不稳定**（不是机制不通）；
- 下一步（小且明确）：在子侧把每次投票的**值**打出来（看是"差一点"还是"完全不同"），据此决定是提高采样轮数
  还是别的；退役是"3 次内任意两次一致"，我实现的语义相同 ⇒ 重点在采样质量。

## 十二、受害者协议端到端跑通（2026-10-11 深夜）

设备逐字：

```
PFSAMPLES 36 kept=256
VOTES vote0=0xffffff8942589200  vote1=0xffffffc03709beb0  vote2=0xffffffc03709beb0   (后两次一致)
main: victim spawned task=0xffffffc03709beb0
main: payload page at 0xffff80000003ec00  fake_task=0xffff80000003fe80  fake_lock=0xffff80000003fa80
main: route status=0/0 attempts=2 calls=4 successes=4 stamp=-1/99 requeue=0 waiter=-1 owner=0
main: victim uid=2000
```

- 本轮修掉的可疑点**全是我照记忆猜错的**（探针就是准绳）：
  ① 采样参数 period/自旋：我写 1/2000 ⇒ 探针真值 5000/500000（与退役 primitives.cpp:68 一致）；
  ② 环长度必须 (1+2^n) 页 ⇒ 用探针的 9 页；③ 走查缓冲必须 base + data_offset（不是 base）；
  ④ 另加 data_size == 0 ? 8 页 : data_size 守卫（探针 :109）与子进程 setvbuf(_IONBF)（否则 _exit 丢打印）。
- ⇒ 协议全通：spawn → 子侧 perf 泄漏并两次一致 → task 送回 → 载荷页 target=child_task+cred → 三步跑 →
  受害者自报 uid（判据通道本身工作正常）。
- 剩下一处：写没落到受害者的 cred 上（uid 仍 2000）⇒ 与写侧"引擎判 OK 但落点未证"是同一处；
  下一步：先用旧码现成的 comm 通道核对 child_task 正确性，再查写落点。
- 待清理（本批）：诊断打印 VOTES/PF* 在写落点确认后同批删除。

## 十三、comm 通道（旧码 M）接通：把"目标对不对"与"写落没落"分开判

设备逐字：

```
main: victim spawned task=0xffffff803a9eda00
main: victim comm baseline len=15 first=103
main: route status=0/0 attempts=2 calls=4 successes=4
main: victim uid=2000
```

- 103 = 字符 g，len=15 = ghostleaf_01234 ⇒ **子进程与协议都对**（prctl 设的名字读得回来）。
- 用途（旧码原话）：分辨叶子写落在 [target] 还是 [target+8]（comm 被清 ⇒ len 0；被写 8 字节 ⇒ len 8；未动 ⇒ 15）
  ⇒ **写一次 comm 字段再读一次**，即可判定：① 泄漏出的 child_task 是否真指向该任务；② 写是否真落下去。
- 下一步（旧码同形的 W3-0: leaf dir）：把 target 换成 child_task + abi.task_struct.comm(1960) 再跑一轮 ⇒ 再读 M：
  len 变 0 或 8 ⇒ **写路径与泄漏目标双双成立**；仍 15 ⇒ 写没落 ⇒ 问题在写侧。
- 待清理（登记）：诊断打印在结论落地后同批删除。

## 14. Write landing verified: THE WRITE DOES NOT LAND (decisive negative)

Device, word for word (target temporarily set to the victim comm field, same shape as the old W3-0):

```
main: victim comm baseline len=15 first=103
main: route status=0/0 attempts=2 calls=4 successes=4
main: victim comm after  len=15 first=103      <- not a single byte moved
main: victim uid=2000
```

- This rules out one whole class: the TARGET IS NOT WRONG (it is the victim own comm, baseline 103/15).
  The problem is on the WRITE side: the chain wins (engine says OK) yet nothing lands.
- Located: this tree is MISSING memory/payload_builder -- the retired payload_write_layout(...) which
  computes the erase tuple (pc/right/left/fops/parent) FROM THE REQUEST. I filled those from the host
  test values instead, so the tuple is wrong and the write lands elsewhere (or nowhere).
- Next (single item): port payload_builder (payload_write_layout + its few inputs; pure, host-testable),
  wire it into the payload page build, rerun this verification: comm after should read len 0/8.
- Registered cleanup: the TEMP-VERIFY marker in main is removed in the same batch once it passes.
