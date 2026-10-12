# 43499 · 第 65 轮：轮间演化 = "拷贝互相覆盖"；本树每轮重建 ⇒ 清掉了它 ✗✓

## 一、候选（✓✓，退役注释自述 ✓）

```
退役（`multicast_waiter_route.cpp:98-102` ✓）：「The copy landing on the caller's stack is a race
against skb page recycling … **Re-poison and re-walk** instead, arming from the point where the
**earlier copies have overwritten each other**.」✓
⇒ ⇒ 关键短语：**arming from the point where the earlier copies have overwritten each other** ✓
   = 下一轮**从"上一轮的拷贝已经互相覆盖"的状态**出发 ✓✓（⇒ 即：**跨轮有状态演化** ✓）；
⇒ 本树（`multicast_route.hpp:411` ✓）：每轮 `build_stamp()` **从零重建** ✗ ⇒
   ⇒ ⇒ **⇒ 每轮都从"干净形状"出发** ✗ ⇒ 若"逐步覆盖逼近"才是落点的机制 ✓ ⇒
      本树**永远走不到那一轮** ✓✓（⇒ 与"15 轮全不落"吻合 ✓）。
```

## 二、这与第 55 轮的对照结论并不矛盾（✓，需要澄清 ✓）

```
第 55 轮我写"每轮重建 = 与退役同义 ✓"✗ —— 依据是同一句里的 **Re-poison** ✗；
但同句后半恰恰说"**arming from the point where … overwritten each other**" ✓ ⇒
⇒ ⇒ 即：**既要 re-poison（重写毒值）✓，又要从"被覆盖过的点"起算** ✓ ⇒ ⇒ 两者**不冲突** ✓；
⇒ 而本树的 `build_stamp()` **重建整个 stamp** ✗ ⇒ 是否等价 ⇒ **取决于它重建了什么** ✗；
⇒ ⇒ **⇒ 下一读：本树 `build_stamp()` 重建的范围 ↔ 退役 re-poison 的范围** ✓（一处对照 ✓）。
```

## 三、下一步（✓）

```
① 读本树 `build_stamp()`（`multicast_route.hpp:275-284` 附近 ✓）⇒ 看它写哪些字 ✓；
② 与退役同处对照 ✓（⇒ 若本树把"被覆盖过的那几个字"也归零 ✗ ⇒ 就是差异 ✓）；
③ 若确认 ⇒ 改为"只重写需要重置的部分" ✓（⇒ 保留覆盖演化 ✓）⇒ 复看 `victim uid` ✓。
判据：任一轮 `main: victim uid=0` ✓。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
