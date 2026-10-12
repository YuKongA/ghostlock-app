# 43499 · 反转：退役的 select 路由**自己也不搬运**（round 228）✓✓✓

## 读码证据（✓，决定性 ✓）

```
快照 :147  std::uint32_t wait_futex = 0, target_futex = 0, chain_futex = 0;   // 三个字都是 0 ✓
全文件 grep：wait_futex 只出现在 :147（定义）与 :175 / :245（作为 futex_op 的参数）✓ ⇒ **无任何赋值** ✗；
而 :245 是 `CMP_REQUEUE_PI(wait_futex, 1, 1, target, 0)` ✗ ⇒ 内核要求 `*uaddr1 == val`（1）✗
⇒ 而它的 `wait_futex` 恒为 0 ✗ ⇒ ⇒ **它的 requeue 必然 EAGAIN** ✗ ✓。
```

## 由此得到的反转（✓）

| 我此前的判断 | 现在（有码为据 ✓） |
| --- | --- |
| "requeue 返回 ≥1 才是成功 ⇒ 我差在 requeue" ✗ | **退役自己也拿不到 ≥1** ✓ ⇒ requeue **不是**成功路径 ✗ |
| "我得让 `cmp_requeue_pi` 成功" ✗ | **不必** ✗ ✓；我此前的 `successes=0` 与退役**同读数** ✓ |

⇒ ⇒ 于是"我这条路线搬不动"**不是缺陷** ✓ —— 真正的判据应该回到**写是否落地**（`enforce` 1→0 ✓）。

## 下一批（✓，方向性 ✓）

```
① 核归档：退役的 **select 路由**是否**曾经**让 `enforce` 归零 ✓（若也从未 ⇒ 该 route 在退役里可能
   也只是"跑到而已" ✗ ⇒ 我的目标应改为"把 multicast 那条的写落地" ✓）；
② 回到清单第 1 项（`payload.value` 是毒值 ✗、真值未喂 ✗）—— 它是**两支共用**的 ✓，
   且已有行号证据 ✓ ⇒ 这才是"写落地"的最短路径 ✓；
③ `buf=264` ✗ 同批核 ✓。
```

## 门禁

```
android → RC=0，0 error / 0 warning ✓；check → 108 PASS ✓；lint → 0 ✓
```
