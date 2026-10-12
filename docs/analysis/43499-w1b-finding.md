# 43499 · 归档的 W1 段给出两条缺失结构（round 119）

## 归档逐字（`...CPP17-...multicast-pass-2.native.log` 第 61-119 行 ✓）

```
[route] … multicast route status=0 clean=1/1 step=0 errno=0     ← W1 轮（selinux_enforcing 写）
**SELinux permissive**                                           ← ★ W1 的写**落地了**（enforcement 关闭）
W1b: private scratch repair attempt 1/3                          ← ★★ W1 之后还有一轮
=== W1b: private scratch repair === target=0xffffff88f5b98108 mode=1 leaf=1
      （同一套 spray + route ✓）
private scratch repaired; releasing quarantine                    ← ★ quarantine 概念
Write 1 complete
…（W2）perf task: …da00 (179/256 votes) ✓；child_task=…da00 ✓；
      prepare_kernel_page ok attempt=1 ✓；**W2 fast repair payload prebuilt** ✓（在 W2 的 spray 内 ✓）
=== W2: cred === target=0xffffff898138e198 mode=2 leaf=0
```

## 我缺的两条

| # | 缺失 | 证据 |
| --- | --- | --- |
| 1 | **W1b**：W1 之后的一轮 `leaf=1` 修复（与 W2b 同形 ✓） | 归档 `W1b: private scratch repair` ✓ |
| 2 | **W1 落地的观测**：归档用 `SELinux permissive` 证明 W1 写成了 ✓；我没有任何对应观测 ✗ | 归档那行 ✓ |

⇒ 因此下一步（可验证 ✓、且判据直接 ✓）：

1. 把 `w1` 也补上一轮 `leaf=1` 修复（**复用我刚为 W2b 建的那套** ✓：第二次准备 + 第二份页面 + 第二轮 ✓）；
   目标取该步骤的**自身修复目标**（退役是 `private scratch` = spray 区里的一块 ✓，具体取法要再看代码 ✓）；
2. 加一条**落地观测**：读 `/sys/fs/selinux/enforce`（启动时 unreadable ✓ ⇒ 写后若能读出 `0` 即证明落地 ✓；
   若仍不可读，则退化为"无观测"并如实记录 ✗）；
3. 判据：① 观测若能读 ⇒ 直接证明机制落地 ✓；② 之后再看 `victim uid=0` ✓。
