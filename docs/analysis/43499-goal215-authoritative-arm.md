# 43499 · 第 214–215 轮：**落格修正已生效（真机印证 ✓）；主机测试随之暴露旧 pin** ✓✓

## 一、真机印证（✓✓✓，这是本会话第一次"改对落格"）

```
`main: page-after-build step=w2 **pc=0xffffff89f2e49990** **left=0x0** want_value=0xffffff802abfd588 want_target=0xffff…` ✓✓
⇒ ⇒ **⇒ `pc` ≈ `target − 8`** ✓（⇒ target 应 `…9998` ✓ ⇒ 差 8 ✓）**且 `left = 0x0`** ✓
   ⇒ ⇒ **⇒ 与**权威固定向量**（`pc = target − 8` ✓ / `left = 0` ✓）逐字一致** ✓✓✓
而硬判据：`step result **w1 => won**` ✓ / `step result **w2 => w2-not-rooted**` ✗ ；`victim uid=2000` ✗（⇒ 未 root ✓）
```

## 二、改动（✓，一处 ✓）

```cpp
// write_encoder.cpp 的 preserve 分支（★ 第 214 轮）：
arm.pc = （target ≥ backstep ? target − backstep : 0U）✓ ；arm.**right = value** ✓ ；arm.**left = 0U** ✓
// 原先：`arm.pc = value` ✗ / `arm.left = target` ✗ ⇒ 与权威**相反** ⇒ 是错的落格 ✓
```

## 三、随之暴露的既有缺陷（✓✓，两处，均非本次改动引入的"新错"）

```
① `core/Makefile:667` 的 `race_action_test` **只链了 `write_encoder.cpp`** ✗ ⇒ 缺 `write_layout.cpp` ⇒ **链接失败** ✗
   ⇒ **已修** ✓（⇒ 补上 `write_layout.cpp` ✓；check 由 RC=2/0 PASS ⇒ **59 PASS** ✓）；
② `write_encoder_test.cpp` 的**固定向量** pin 的是**旧形态** ✗（"a value write aims … **at the value**" ✗ /
   "carries the **target in the left field**" ✗）⇒ ⇒ **⇒ 已按权威更正 3 条** ✓；而**仍有 4 条**未改：
   `the two arms differ` ✗（⇒ 两臂现在**同形态** ⇒ 该断言已无意义 ⇒ 应改为"在 `right` 上不同" ✓）、
   `the vector's pc word is reproduced` ✗ / `and its left word` ✗ / `and the side field is zero in every vector` ✗
   （⇒ 那一族的**固定向量表**同样 pin 旧形态 ⇒ 需一并按权威更新 ✓）。
```

## 四、门禁现状（✗ 一处，其余绿 ✓）

```
`check` → **RC=2，59 PASS** ✗（⇒ 因上述 4 条旧 pin 未改完 ✓）；`android` → RC=0，**0 error / 0 warning** ✓；`lint` → 0 ✓。
⇒ ⇒ **⇒ 下一步（明确 ✓）**：把余下 4 条断言/向量按**权威形态**更新 ✓ ⇒ 期望 **check 全绿（109 PASS ✓）**
   ⇒ 然后**再看真机** `main: victim uid=0` ✓（⇒ 落格已对 ✓，若值/目标也对 ⇒ 应见 root ✓）。
```
