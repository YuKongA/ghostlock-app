# 43499 · trigger 移位后的状态与一个未解的 `2/61`（round 90-91）

## 已做（结构改动 ✓，方向被证实 ✓）

- `trigger()` 从"等待者 park 返回之后"移到**调用线程、等待者仍挂着时**（`execute()` 里 `requeue()` 之后 ✓）；
  注释写明缘由（退役原文：拷贝落在"**此刻还在**的那份 waiter 形状上" ✓）。
- **证据**：等待者的 park 返回值由 `ETIMEDOUT(110)` 变成 **`EPERM(1)`** ✓ ⇒ 触发**确实碰到了仍在 park 的等待者** ✓。

## 未解（精确描述 ✓）

```
TEMPSTART enter + TEMPSTART ok   × 15      ← start_threads() **每次都成功** ✓
TEMPWAIT waiter ret=-1 errno=1   × 15      ← park 被触发影响（EPERM ✓）
main: route status=2/61 attempts=8 …         ← 但路由报 FallbackSafe/step 61 ✗
TEMPBISECT got=12672 at2000=0x0             ← 仍无写入 ✗
```

⇒ 全树只有 `multicast_route.hpp:235` 报 61 ✓（`if (!start_threads()) …`）⇒ 与"它每次成功"**矛盾** ✗；
  两种可能：① `report_` 在某条路径上被**旧值**覆盖（成功之后又走了失败分支 ✗）；
  ② `stop_threads()` 之后的某处再次写 `report_.status`（grep 未见 ✗）。

## 下一步（两点，各自可验证）

1. 在 `execute()` 的**每个** return 前打一行（含 step/code）⇒ 定位 61 是谁写的 ✓；
2. 把 `TEMPBISECT` 的读回改成"**在窗口内部**读"（现在读得太晚，缓冲可能已被消费/回收 ✓）——
   即在 `trigger()` 返回后立刻从对端 `recv` 一次 ✓（不带 `DONTWAIT` ✓）。

## 追加（round 92）：61 的来源仍未定位，且四个拒绝点均未触发

给四个拒绝点（58/59/60/61）各加 `TEMPRETRY step=…` 后真机再跑：

```
TEMPSTART enter + ok  × 15        ← start_threads 每次都成功 ✓
（无任何 TEMPRETRY 行）             ← 四个拒绝点**一个都没走** ✗
main: route status=2/61 …          ← 却报 FallbackSafe/step 61 ✗
```

⇒ 结论：`report_.status` 的 61 是**别处**写入的（四个 `refusal(…)` 调用点都不是）⇒
  下一步探针：在 `stop_threads()` 内与 `execute()` 的成功出口各打一行 `report_` 的数 ✓。

（这是诊断层面的异常 ✓，**不影响**主线判据：`TEMPBISECT at2000` 与 `victim uid`。）
