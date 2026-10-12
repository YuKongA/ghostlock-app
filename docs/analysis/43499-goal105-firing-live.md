# 43499 · 第 105 轮：归档第二行也生效 ⇒ 第 80 轮第二类**两行齐**（`prebuilt` + `firing`）✓✓✓

## 一、改动（✓，一处打印 ✓）

```cpp
// `if (repair_prebuilt) { … }` 体内、发送之前（= 退役 `w2_fast_repair_activate` 的激活点 ✓）
std::printf("main: W1b: firing prebuilt init_cred+8 repair step=%s target=0x%llx", …);
```

## 二、真机判据（✓✓✓，两行齐 ✓）

```
`main: fast repair payload **prebuilt** step=w1 base=0x…` ✓（第 103 轮 ✓）；
`main: W1b: **firing prebuilt init_cred+8 repair** step=w1 target=0xffff8000000f0108` ✓（本轮 ✓）；
`main: W1b: **firing prebuilt init_cred+8 repair** step=w2 target=**0xffffff802abfd590**` ✓✓；
  ⇒ ★ `w2` 的目标逐字 = `init_cred + 8` ✓（**与归档同** ✓）；
其余：`main: victim uid=2000` ✗（一如往常 ✓）；门禁：check **109 PASS** ✓ / android **0 error / 0 warning** ✓。
```

## 三、第 80 轮"缺失三类"的最终状态（✓）

| # | 条目 | 状态 |
| --- | --- | --- |
| 1 | `WNb: private scratch repair` | **闭合** ✓（归档亦 `attempt 1/3` ✓ ⇒ 只差打印 ✓ 已补 ✓） |
| 2 | `fast repair payload prebuilt` + `firing prebuilt init_cred+8 repair` | **两行齐** ✓✓（第 103/105 轮 ✓）—— 且**行为链已通** ✓（旧处 `page=` = 预建 `base=` ✓） |
| 3 | `child is root!` / `SELinux permissive` | 成功的结果 ✓（尚未到达 ✓） |

```
⇒ ⇒ **⇒ 第 80 轮列出的"我缺失的三类"，前两类已全部补齐** ✓（⇒ 唯独第三类是"成功后才会有"的结果行 ✓）。
```

## 四、下一步（✓，剩下的两小件 ✓）

```
① 该次路由的进入延迟取 **5000 µs** ✓（第 90 轮：退役 `race.reset(fast_repair ? 5000 : select_enter_delay_us)` ✓，
   ⇒ 本树 race 层无该入口 ✗ ⇒ 需补一个参数 ✓）；
② 出口（含早退）补 `discard` ✓（守则 §5 ✓）；⇒ 判据：**再看 `victim uid`** ✓。
```

## 五、门禁（✓，全绿 ✓）

```
check → **109 PASS** ✓；android → RC=0，**0 error / 0 warning** ✓；lint → 0 ✓
```
