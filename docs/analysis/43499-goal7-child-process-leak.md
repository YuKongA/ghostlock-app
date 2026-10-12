# 43499 · 新目标第 7 轮：第二耗尽的**结构性根因**（子进程 pid 随栈销毁）✓✓✓

## 一、根因（✓，结构级 ✓）

```
prepare_page.hpp:130  struct PrepareState { pre_pids[..] / post_pids[..] / spray_pids[..] / leak_pid } ✓
prepare_page.hpp:151  kill_children(state) —— 按顺序 stop_child 全部四组 ✓
调用点：**:268 / :287 / :311 / :325 / :344 / :361** —— 全部是**早退/局部**路径 ✓
而**成功路径**（:470-476）只有 `close_them_all`（关 fd ✓）⇒ **不停子进程** ✗；
且 `PrepareState` 是 **prepare 的局部栈对象**（:224 ✓）⇒ 返回即销毁 ⇒ pid 丢失 ✗；
`PrepareReport`（steps/prepare_runner.hpp:50-71 ✓）**不含任何 pid** ✗ ⇒ **调用方无从回收** ✓。
⇒ ⇒ 于是每轮 prepare 遗留 ~450 个子进程（真机计数 pre=284 / post=32 / spray=191 ✓）
   ⇒ 5–6 轮触 EAGAIN（errno=11 ✓）⇒ `victim-fork-failed` ✓✓。
```

## 二、为什么第 5 轮的 fd 修复没解决它（✓）

```
fd 与 pid 是**两套资源** ✓：`kMaxReclaimSteps` 修好 ⇒ fd 恒 3 ✓（判据 ✓），
但子进程依旧累积 ✗ ⇒ 第二种耗尽独立存在 ✓ ⇒ 本轮的定位与上一轮并不重复 ✓。
```

## 三、两条修法（✓，下一步二选一）

| # | 修法 | 性质 |
| --- | --- | --- |
| A | 让 `prepare` 在**成功返回前**也 `kill_children` ✓ | 最小改动 ✓，但需确认"窗口/路由"不再依赖这些子进程的内存形状 ✗ |
| B | 把四组 pid **随报告交出** ✓（⇒ `PrepareReport` 增字段 ✗ = 契约变更 ⇒ L 级 ⇒ 先计划 ✓），由调用方在步骤结束时停 ✓ | 结构正确 ✓，代价较大 ✓ |

```
判据（两法同）：`main: victim fork failed errno=` 不再出现 ✓ ⇒ 15 轮跑满 ⇒ 读到 15 次 `victim uid` ✓；
⇒ 只有跑满，才能回答"侧臂/W2b 修正后写能否落地"（本轮仍未达 ✓）。
```

## 四、门禁（✓）

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
