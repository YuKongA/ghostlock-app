# 43499 · 第 132 轮：**主 route 目标已改对（`…798`）⇒ 还差修复轮那一处** ✓✓✓

## 一、改动（✓，一处、一行 ✓）

```cpp
// main.cpp:654 —— 由 `victim_comm_off`（`abi.task_struct.comm` = 0x7a8 ✗）改为 `victim_cred_off`（`…cred` = 0x798 ✓）
const std::uint64_t victim_target = spawned.task + **victim_cred_off**;   // ★ 第 132 轮 ✓
⇒ 且 `victim_cred_off` **早已读入并校验非零** ✓（`:643-645` ✓）⇒ ⇒ **⇒ 确实只是"一行之误"** ✓✓。
```

## 二、真机（✓✓✓，目标形态已与归档同族 ✓）

```
`main: step=w2 target=0xffffff8005040**798**` ✓ ；`…25b7f**398**` ✓ / `…40acb**d98**` ✓ /
`…25f83**d98**` ✓ / `…2d2e7**398**` ✓ ⇒ ⇒ **⇒ 全部以 798 结尾** ✓（= `cred` 字段 ✓）⇒ 与归档 `…19880798` **同族** ✓✓；
硬判据：`main: victim uid=2000` × **15** ✗ ；`w1 => won` ✓ / `w2 => w2-not-rooted` ✗；
门禁：`android` RC=0，**0 error / 0 warning** ✓；`check` **109 PASS** ✓。
```

## 三、还差的那一处（✓，第 131 轮已列为② ✓）

```
**修复轮**（`main.cpp:838`）的目标仍是 `kAliasBase + init_cred_image + alias_image_delta + 8U` = `init_cred + 8` ✗；
⇒ 而**那才是"把 victim 的 cred 指针改成 init_cred"的那一步** ✗✓（⇒ 退役 `w2_fast_repair_activate` ✓）；
⇒ ⇒ ⇒ **⇒ 修法**：把该处目标也改为 **`spawned.task + victim_cred_off`** ✓（⇒ 与主 route 同式 ✓，
   退役 `steps.cpp:165` 的"主与修复同目标" ✓）⇒ ⇒ **⇒ 判据：`main: victim uid=0`** ✓。
```

## 四、门禁（✓，全绿 ✓）

```
check → **109 PASS** ✓；android → RC=0，**0 error / 0 warning** ✓；lint → 0 ✓
```
