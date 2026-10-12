# 43499 · 第 56 轮：触发层**全部同形** ⇒ 用户态全对齐 ✓✓

## 一、待确认项的答案（✓✓，退役注释逐字 ✓）

```
退役（`multicast_waiter_route.cpp:98-102` ✓）：「The copy landing on the caller's stack is a race
against skb page recycling, so a single setsockopt is a single lottery ticket: one miss costs the
whole run, because a miss usually panics the kernel. **Re-poison and re-walk** instead, arming
from the point where the earlier copies have overwritten each other.」
⇒ 「Re-poison and re-walk」= **每轮重做** ✓ ⇒ 本树"每轮 `build_stamp()`"**同义** ✓ ⇒ **待确认项关闭** ✓。
```

## 二、触发层六项终表（✓，全同）

| 项 | 本树 | 退役 | 判定 |
| --- | --- | --- | --- |
| 轮上限 | `max_attempts`（128） | 默认 128 / profile 可覆盖 | 同 ✓ |
| 装填 | `attempt < arm_sequence`（16） | 同 | 同 ✓ |
| 落地判据 | `shape_landed(...)` | `!= EADDRNOTAVAIL` | 同义 ✓ |
| 触发 | `consumer_go` + `arm_hold` 次 yield（20000） | 同 | 同 ✓ |
| 成功判据 | `consumer_success > 0` | 同 | 同 ✓ |
| 每轮重建 | 每轮 `build_stamp()` | 「Re-poison and re-walk」 | 同 ✓ |

## 三、这轮的意义（✓✓）

```
用户态**全部对齐** ✓：写值 ✓ / 目标 ✓ / 臂 ✓ / 编码 ✓ / 页内容（逐字相等 ✓）/ 采纳 ✓ /
触发循环（六项 ✓）/ 重试（15 轮 ✓）/ 排空（slab_drain ✓）/ 直映射守卫（生效 ✓）；
⇒ ⇒ **⇒ "写未发生"的证据只能来自内核侧** ✗ ⇒ 下一步必须把"内核到底看没看重我们那页"作为问题 ✓
   （⇒ 而本树已有的一条**内核侧**读数：`stamp=-1/99` ✓ = "拷贝落地" ✓ ⇒ 只说明拷贝到栈 ✓，
     不说明 walk 用它 ✗ ⇒ ⇒ **⇒ 需要一条"walk 是否走了我页里的指针"的证据** ✓）。
```

## 四、下一步（✓）

```
按「查旧代码」✓：退役如何在**不需要新读回**的前提下知道"walk 用了我们那页"？
  · 候选：`stamp_errno` 的具体值 ✓（EADDRNOTAVAIL 之外的组合意味着什么 ✓）；
  · 候选：`arm_hold` 期间的 `consumer_calls/success` 组合 ✓（本树已打印 ✓）；
  · 候选：退役 `route_controller.cpp` / `route_lifecycle.hpp` 里的**状态机**判据 ✓ → 下一读 ✓。
判据不变：任一轮 `main: victim uid=0` ✓。
```

## 五、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
