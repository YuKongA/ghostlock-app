# 43499 · 第 104 轮：**第二半生效**（预建 → 激活链已通，且用的是预建那份页）✓✓✓

## 一、改动（✓，一处 ✓）

```cpp
// 旧处：由"目标非零"改为由**预建标记**驱动 ✓（= 退役 `w2_fast_repair_activate` 只在 prebuild 成功后跑 ✓）
if (repair_prebuilt) {                                  // 第 104 轮 ✓
    { …（原 else 体内的"激活 + 发送 + 路由" ✓）… }
}
```

## 二、真机判据（✓✓✓，链已对齐 ✓）

```
`main: fast repair payload prebuilt step=w1 base=0xffff800000037180` ✓ ⇒ 旧处 `base=0xffff800000038000 page=**0xffff800000037180**` ✓✓；
`main: fast repair payload prebuilt step=w2 base=0xffff80000000f180` ✓ ⇒ 旧处 `base=0xffff800000010000 page=**0xffff80000000f180**` ✓✓；
⇒ ⇒ ★ **旧处报的 `page=` 恰等于预建时的 `base=`** ✓ ⇒ ⇒ **⇒ 修复路由用的正是"预建那份页"** ✓（名副其实 ✓）；
其余：`route status=0/0 clean=1/1 attempts=16` ✓ / `mcast ghost disarm ret=-1 errno=110` ✓ / refused 计数 **0** ✓ /
      `main: victim uid=2000` ✗（一如往常 ✓）。
```

## 三、第 80 轮"缺失三类"的现状（✓）

| # | 条目 | 状态 |
| --- | --- | --- |
| 1 | `WNb: private scratch repair` | **已闭合** ✓（第 88 轮：归档亦 `attempt 1/3` ✓，只差打印 ✓ 已补 ✓） |
| 2 | `fast repair payload prebuilt` / `firing prebuilt init_cred+8 repair` | **预建 ✓ 激活 ✓ 链已通** ✓（本轮 ✓；仅"`firing …` 那行打印"未补 ✗） |
| 3 | `child is root!` / `SELinux permissive` | 成功的结果 ✓（尚未到达 ✓） |

## 四、下一步（✓，两小件 ✓）

```
① 补归档第二行打印 `main: W1b: firing prebuilt init_cred+8 repair` ✓（w2 时 ✓，一行 ✓）；
② 该次路由的进入延迟取 **5000 µs** ✓（第 90 轮：退役 `race.reset(fast_repair ? 5000 : …)` ✓）；
③ 出口（含早退）补 `discard` ✓（守则 §5 ✓）；⇒ 判据：**再看 `victim uid`** ✓。
```

## 五、门禁（✓，全绿 ✓）

```
check → **109 PASS** ✓；android → RC=0，**0 error / 0 warning** ✓；lint → 0 ✓
```
