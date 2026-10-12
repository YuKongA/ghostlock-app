# 43499 · 每轮重新下毒（re-poison）：已落，未翻（round 155）

## 依据（退役注释逐字 ✓）

```
"The copy landing on the caller's stack is a race against skb page recycling, so a single
 setsockopt is a single lottery ticket: one miss costs the whole run, because a miss usually
 panics the kernel. **Re-poison and re-walk instead**, arming from the point where the earlier
 copies have overwritten each other."
⇒ 每轮都要**重建**那份形状 ✓；本树 build_stamp() 原先只在循环外调用一次 ✗
```

## 已改（本树 ✓）

```cpp
// multicast_route.hpp：把 build_stamp() 放进 attempt 循环的第一件事
for (attempt = 1 … max_attempts) { if (!build_stamp()) break; … }
```

## 真机（4 跑 ✓）

```
try1/2：奇偶不过 ⇒ 提前停 ✓（分布不变）
try3：refused (victim-leak-disagreed)   ← 既有守卫的罕见路径 ✓（新出现一次，登记观察 ✓）
try4（完整）：selinux enforce=1 ✗   comm after len=15 ✗   uid=2000 ✗
⇒ 本轮改动**未翻转结果** ✗（如实 ✓），但它有退役注释为据 ✓ ⇒ 保留 ✓
```

## 备注（纪律 ✓）

- `victim-leak-disagreed` 是新观察到的拒绝名 ✓：它是**既有**守卫 ✓（我的改动只写引擎成员缓冲
  `stamp_` ✗，不可能影响内核侧泄漏 ✓）⇒ 登记为观察项 ✓，下一批跑继续统计其一阶频率 ✓。
