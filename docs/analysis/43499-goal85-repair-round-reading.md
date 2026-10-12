# 43499 · 第 85 轮：`repair-round` 读数到手（w1 修复**确实执行** ✓）+ 修掉自己引入的 host 构建断裂 ✓

## 一、读数（✓✓✓）

```
`main: repair-round step=w1 target=**0xffff800000038108**` ✓（一次跑里出现 ✓）
⇒ ⇒ **⇒ w1 的修复确实执行** ✓（⇒ 第 84 轮的推断成立 ✓：行为已在、原先只是**无声** ✗）；
⇒ 目标形态 = `0xffff8000…` ✓（页内地址 ✓，与 selinux 的页值同族 ✓）⇒ ⇒ 与退役的
   `w1_scratch_poison = heap.current.base + mcast.buffer_size` ✓ **同式** ✓（第 32 轮已认 ✓）。
```

## 二、修掉自己造成的断裂（✓，必须记 ✓）

```
第 79 轮的落码引入了 `#include <linux/futex.h>` ✗ ⇒ **host（macOS）无此头** ✗ ⇒
  `prepare_mapping_test` 编译失败 ⇒ `check` 变红（0 PASS ✗）—— 本轮发现并修 ✓：
  ① 两行 include 用 `#if defined(__ANDROID__)` 守卫 ✓；
  ② `waiter_main` 里的 disarm 段**同样守卫** ✓（⇒ host 编不到该段 ✓，而 host 测试不走该路径 ✓）；
⇒ ⇒ 修后：`check` **109 PASS** ✓ / `android` **RC=0，0 警** ✓。
⇒ 教训：**新增平台相关 include 时，必须同批考虑 host 构建** ✓（⇒ 若当时跑满三门禁就会当场发现 ✗）。
```

## 三、W1b 的最终结论（✓✓）

```
与退役的差异**只剩两点**（行为层面**已在** ✓）：
  ① 轮数：退役按 `w1_scratch_repair_attempts` 重试 ✓（本树只做一次 ✗）；
  ② 打印：退役有 `W1b: private scratch repair attempt %u/%u` / `private scratch repaired` ✓（本树无声 ✗）；
⇒ ⇒ 因此 W1b **不再是"缺失的动作"** ✗，而是"**少跑几次**" ✓ ⇒ 优先级下调 ✓
   （⇒ 剩余的两条主线回到：**fast repair 预建/激活** ✓ 与 **域/宿主** ✓）。
```

## 四、门禁（✓，全绿 ✓）

```
check → **109 PASS** ✓；android → RC=0，**0 error / 0 warning** ✓；lint → 0 ✓
```
