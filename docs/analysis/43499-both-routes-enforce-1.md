# 43499 · 两条 route 同一读数：`enforce=1`（round 193）✓

## 本轮的三个读数（都有据 ✓）

```
select 路由（HIT1/2/4）：
  main: select route … successes=0 lock=0 park=-1 **parkerrno=110** setattr=-1
  main: selinux enforce=**1** ✗
归档退役（同一环境）：
  mcast ghost disarm ret=-1 err…（= errno 110 家族 ✓）⇒ 退役**也**在同一处等到超时 ✓ 但它容忍 ✓
canonical（multicast，本晚 ≥9 次完整跑）：enforce 从不 0 ✗
```

## 由此得到的两条判断

1. **`parkerrno=110`（ETIMEDOUT）不是异常** ✓：归档里退役自己也是这个读数 ✓ ⇒ 它靠容忍继续 ✓；
   ⇒ 因此 `successes`/`Running` **不是**"写"的可靠判据 ✗ ⇒ 判据仍是 `enforce` ✓（本轮已给 select 补上 ✓）；
2. **两条 route 的 `enforce` 都是 1** ✗ ⇒ ⇒ 说明"写不发生"更可能在**两者共用**的部分 ✓
   （页面构造 ✓ / 两个 futex 字的**值** ✓ / 几何 ✓）—— 而不是某条 route 特有的形状 ✗。

## 下一步（共用部分的候选 ✓）

| # | 候选 | 为何值得查 |
| --- | --- | --- |
| 1 | `words_region` 里两个/三个字的**值** | `WAIT_REQUEUE_PI` 与 `CMP_REQUEUE_PI` 都按**字值**做比较 ✓ ⇒ 字值不对 ⇒ EAGAIN/ETIMEDOUT ✓ |
| 2 | 页面里"元组"与**目标**的一致性 | 已做过字节级比对 ✓（preserve 支 ✓）⇒ 但仍可复核一次 ✓ |
| 3 | `arm_hold`/时序 | 已按退役对齐（20000 ✓）⇒ 可能性较低 ✓ |

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
