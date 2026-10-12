# 43499 写落地 · 奇偶修好 + 等待者已停住（2026-10-11 夜，round 61）

## 两项新事实（真机，逐字）

```
main: layout matches=1 accepts=1 value=0xffff7ffd6fab8d00 mode=1   ← 奇偶判据通过（确定性）
TEMPWAIT waiter ret=-1 errno=110                                    ← ETIMEDOUT：等待者停住了
main: route status=0/0 attempts=2
main: victim uid=2000                                             ← 仍未落地
```

- 载荷映射改为**独立的 1 MiB**（16 个 64 KiB 候选 ⇒ 必有奇偶合格者 ✓），`close()` 一并归还 ✓；
  取不到合格页则具名拒绝（不退回旧路径 ✓）。
- 等待者探针终于跑到：`ret=-1 errno=110`（ETIMEDOUT ✓）⇒ **等待者确实停在 `wait_word` 上** ✓、
  并与退役的形状一致（退役也让它停在那里 ✓）——所以"停不住"这个怀疑**被排除** ✓。

## 剩下的精确问题（与归档对照）

| 项 | 归档（成功） | 我（现在） |
| --- | --- | --- |
| 等待者 | `waiter parked; owner started` ✓ | `ret=-1/110`（停住 ✓） |
| requeue | `CMP_REQUEUE_PI ret=-1 errno=35`（EAGAIN，且**看到了**目标状态） | `requeue=0`（**一个都没看到** ✗） |
| owner | `owner started` ✓ | `owner=0` ✓ |

⇒ 新的问题是精确的：**为什么 `cmp_requeue_pi` 看不到停在 `wait_word` 上的等待者** ✗；
  归档那次的 `EAGAIN` 说明它**看到了**目标的状态（只是 cmp 值不匹配）⇒ 两边差在"requeue 的 cmp 参数"
  或"链字的选择"上 ✓。

## 下一步（一次探测）

1. 在引擎的 requeue 调用前后各打一行：`wait_word` 的**值**、`target` 的值、以及 cmp 用的期望值；
2. 与归档的 `ret=-1 errno=35` 对照 ⇒ 判定是"cmp 期望值不对"还是"requeue 用错了链字"；
3. 逐项对齐后再谈 `WriteMode::Credential`（归档为 `mode=2` ✓）与 `after uid=0` 判据。
