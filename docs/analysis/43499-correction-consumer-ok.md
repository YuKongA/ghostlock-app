# 43499 · 更正 + 常数全对齐：代码等价、结果不同（round 151）

## 更正（我上一条推错了 ✗）

```
我曾说"我的 consumer_success 是假成功" ✗ ⇒ 读退役 race/threads.cpp:144-155 后**否证** ✓：
  退役：sched_ret = sched_setattr_tid(tid, nice); 若 != 0 用 FUTEX_LOCK_PI 兜底；
        若最终 == 0 ⇒ consumer_success++     ← 与我的实现**语义相同** ✓
⇒ 所以"假成功"假设不成立 ✗（两边的 OK 都表示"系统调用走通了" ✓）
```

## 同一段代码里的常数：**全部对上** ✓

| 项 | 退役 | 我 |
| --- | --- | --- |
| nice 轮转模数 | `PSELECT_CONSUMER_NICE = 19`（compact 时 `(calls%19)+1` ✓） | `nice_modulus = 19` ✓ |
| 兜底锁超时 | 50 ms ✓ | `fallback_lock_us = 50000` ✓ |
| 策略 | `sched_setattr(…, nice)` ✓ | `policy = 0` ✓ |
| arm 常数（上一轮） | 128 / 16 / 20000 ✓ | 128 / 16 / 20000 ✓ |

## 现状（如实）

```
可比对的代码/常数/几何/数值：**全部对齐** ✓（含本轮与上一轮）
结果：完整跑里 `selinux enforce=1` ✗、`uid=2000` ✗ ⇒ **写入未发生** ✗
内核：归档与我**同一个 build**（5.15.189…ab14546557 ✓）
```

## 剩余候选（下一步，按成本排序）

1. **页面字节级对照**：把本树构建的页面与退役 `encode_multicast_waiter` + `PayloadWriteLayout` 的**期望字节**逐字节比 ✓（尚未做过 ✗）；
2. 换 route（`select_stack` / `tcp_zerocopy` ✓）；
3. 内核侧观测（需 root ✗，大概率不可用 ✗）。
