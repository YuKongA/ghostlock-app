# 占位 backend 调研：64560 / 31431 / 43503 / 23274（2026-10-03）

## 目的与范围

调查 4 个仍是占位符的 backend（`cve_2026_64560` / `cve_2026_31431` / `cve_2026_43503` /
`cve_2026_23274`）的上游性质，与现有 `cve_2026_43499` 对照，判断**哪些组件可共享、哪些必须是
backend 私有**，为架构（route/primitive 归属）提供依据。

数据来源：MITRE CWE API（`cveawg.mitre.org`，Linux assigner）的 CVE 记录与 `git.kernel.org` 修复
提交的提交说明。**只是描述层调研**：不等价于可利用性验证，也未做真机；MITRE 文本本身是修复提交
说明，可能存在省略。

## 43499 基线：机制分解

- **根因**：`kernel/locking/rtmutex.c` `remove_waiter()` 误清 `current->pi_blocked_on`（应为
  `waiter->task->pi_blocked_on`），留下指向**内核栈上 `rt_mutex_waiter`** 的悬垂指针（UAF）。
- **触发**：3-futex / 3-thread PI 环 → `FUTEX_CMP_REQUEUE_PI` → `RT_MUTEX_FULL_CHAINWALK` →
  `-EDEADLK` 回滚。
- **写原语**：pselect / tcp_zerocopy / multicast 用可控结构覆盖 stale waiter → `rb_erase` 左子
  重连 → `*dest = value`（单字，带结构副作用）。
- **支撑件**：KASLR slide（pselect 竞态 + nfulnl/boot_id 泄漏）、heap spray / payload 编码、
  victim（perf 泄漏 task + pipes）、W1/W2/W3、handoff、`KernelMemory`（Tier1 引导 + B/C 通道）。
- **组件轴**：frontend/handoff · backend 步骤 · route（select/tcp/multicast）· primitive
  （PiRace/payload/write）· session 状态 · profile。

## 逐个调研

| CVE | 子系统（affected files） | 原语类别 | 触发/能力 | CVSSv3.1 | 与 43499 相同 | 与 43499 不同 |
|---|---|---|---|---|---|---|
| **64560** | `kernel/{exit,signal}.c`、`time/posix-cpu-timers.c` | **UAF**：非 leader `exec()` 竞态，`k_itimer`/`posix_cpu_timer`（timerqueue 节点）被 `timer_delete` 释放后仍被访问 | `timer_create/settime/delete` + `exec()` 竞态；栈上 `k_itimer` 变体（`do_cpu_nanosleep`） | 7.8 HIGH (AV:L) | **同为 UAF**；都需要泄漏 + 堆喷 + 写原语 + cred/seccomp 目标 | 对象不同（timer vs waiter）；触发不同（timer syscall vs futex PI）；**route 不可用** |
| **31431** | `crypto/{af_alg,algif_aead,algif_skcipher}.c`、`include/crypto/if_alg.h` | **内存破坏**：`algif_aead` 就地（in-place）操作但 src/dst 映射不同 → 用错地址/重叠写 | `AF_ALG` AEAD socket | 7.8 HIGH | 都需要某种内存破坏→提权 | 完全不同的子系统与触发；无 waiter/无 PI；route 不可用 |
| **43503** | `net/core/{skbuff,gro}.c`、`net/ipv4/tcp_output.c` | **写 page cache**：漏传 `SKBFL_SHARED_FRAG`，ESP `authencesn-ESN` 杂散写落到 root 只读文件的 page cache | 本地 + `nft dup to <local>`/`nf_dup_ipv4` 等 | 8.8 HIGH (Scope:C) | 同为本地产权提升的目标 | **直接改只读文件**（可能连 SELinux/cred 步骤都不需要）；非任意内核写；route 不可用 |
| **23274** | `net/netfilter/xt_IDLETIMER.c` | **未初始化 `timer_list`**：rev0 复用 ALARM 标签的 timer → `mod_timer()` on garbage | `iptables`/`nft` IDLETIMER rev0 + 已存在的 ALARM 标签 | 7.8 HIGH | —（无明确 UAF/写原语） | 描述层面像 DoS/panic；CVSS 却给 C/I/A:H，存在张力，需核实能否升级为可控写 |

## 组件可共享性矩阵

图例：`✓` 可共享（backend 无关）· `✗` backend 私有 · `?` 需先有对应能力/未验证。

| 组件（按本次架构） | 43499 | 64560 | 31431 | 43503 | 23274 |
|---|---|---|---|---|---|
| frontend / handoff（post-exploit 接管） | ✓ | ✓ | ✓ | ✓ | ✓（但若不需要提权则不适用） |
| session 状态 / `ExploitSession` | ✓ | ✓ | ✓ | ? | ? |
| pipeline / catalog / contracts | ✓ | ✓ | ✓ | ✓ | ✓ |
| profile 传输（GLK1 v2） | ✓ | ✓ | ✓ | ✓ | ✓ |
| `KernelMemory` **接口**（中性能力契约） | ✓ | ✓ | ✓ | ✓ | ✓（前提是能建立通道） |
| W1/W3 **目标语义**（关 SELinux / 清 seccomp） | ✓ | ✓（若走提权） | ✓ | 可能不需要 | ? |
| W2（victim + cred 覆盖） | ✗ | ✗（对象/协议不同） | ✗ | 可能不需要 | ? |
| **primitive**（PiRace/payload/write） | ✗ | ✗ | ✗ | ✗ | ✗ |
| **route**（select/tcp/multicast） | ✗ | ✗ | ✗ | ✗ | ✗ |
| KASLR slide / 泄漏 | ✗ | ✗ | ✗ | ✗ | ✗ |
| heap spray / payload builder | ✗ | ✗ | ✗ | ✗ | ✗ |
| victim 协议（perf 泄漏 + pipes） | ✗ | ?（若可复用 per-task 发现） | ✗ | ✗ | ✗ |
| `KernelMemory` **实现** | ✗ | ✗ | ✗ | ✗ | ✗ |
| backend 步骤实现 / profile 字段 | ✗ | ✗ | ✗ | ✗ | ✗ |

## 结论

1. **四个占位 backend 与 43499 的 primitive/route 完全不可共享**：对象、触发、写原语都不同。
   这印证前面的判断——`route`（stale-waiter 覆盖手法）必须 backend 私有，primitive 同理。
2. **可共享的是“框架件”**：handoff、session、pipeline、profile 传输、contract、`KernelMemory`
   **接口**，以及在目标一致（提权）时的 W1/W3 目标语义。这些正是本次重写要中性的部分。
3. **64560 是唯一同“UAF 类”的**：理论上可共享“UAF 利用框架”（泄漏 + 堆喷 + 写原语抽象 + cred/
   seccomp 目标），但具体 primitive/route/写原语不同。若将来抽公共层，抽的是**利用框架接口**，
   不是 route 实现——仍不支持“route 作为跨 backend 轴”。
4. **43503 的目标模型不同**（改文件而非改内核状态）：它可能不需要 W1/W2/W3 那套“提权后状态”。
   说明 backend 的“目标”也不总是同一套——`Pipeline` 仍应允许 backend 自带步骤语义。
5. **23274 存疑**：描述像 DoS/panic，CVSS 却是 RCE 级；在作为 backend 前需单独核实是否有可控写。
   现阶段按“不可用作提权 backend”处理。

## 对架构的含义

- 支持本次重写结论：**primitive 与 route 归机制/backend 私有**；`KernelMemory` 接口中性、
  实现私有；frontend/handoff、session、pipeline、profile、contract 中性。
- 支持“不把 route 承诺为跨 backend 复用轴”，但保留“同族（UAF）可抽利用框架接口”的口子。

## 未验证 / 风险

- MITRE 记录是修复提交说明，**不等于可利用性**；四者的真实原语、可控性、写原语均未验证。
- 23274 的 CVSS(7.8, C/I/A:H) 与“仅告警/panic”的描述矛盾，需进一步核实。
- 全部未过真机；未标 `supported`。

## 来源

- MITRE CWE API：`https://cveawg.mitre.org/api/cve/<CVE-ID>`（Linux assigner，PUBLISHED）。
- 修复提交：见各 CVE 记录的 `git.kernel.org/stable/c/*` 引用。
