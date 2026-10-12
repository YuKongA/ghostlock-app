# 43499 · 第 130 轮：**归档的 w2 目标 ≠ `init_cred + 8`** —— 极可能是根因 ✓✓✓

## 一、两组数字（✓✓✓，全部来自归档原文 ✓）

```
归档 `:46`  `[*] init_cred **image=ffffffc082bfd588** alias=**ffffff802abfd588**` ✓（⇒ 同一对象的两个视图 ✓）；
归档 `:114` `[*] === W2: cred === **target=0xffffff8a19880798** mode=2 leaf=0` ✓；
本树 `:838` `repair_request.target = kAliasBase + init_cred_image + alias_image_delta + 8U` ⇒ **0xffffff802abfd590** ✗。
```

## 二、算出来的差（✓✓✓）

```
归档 alias（= init_cred 的别名视图）= `0xffffff802abfd588` ✓；
归档 target                              = `0xffffff8a19880798` ✗；
⇒ **差 = `0x9eec83210`**（≈ 42.7 GiB ✗）⇒ ⇒ **⇒ 二者**不在同一区域** ✓；
低位：target `…19880798` ✓ vs alias `…2abfd588` ✓ ⇒ **低 32 位也不同** ✗；高位：`8a…` ✗ vs `80…` ✗。
⇒ ⇒ ⇒ **⇒ 结论：归档的 w2 写目标**不是** `init_cred + 8`** ✗✓；
   ⇒ 而两视图差（image→alias）= `0xffffff802abfd588 − 0xffffffc082bfd588` = **−0xbfc0040000** ✓（另一位面 ✓）。
```

## 三、含义（✓✓✓）

```
本会话几十轮里，**写值**（`init_cred` 别名 ✓）、**页内容**（逐字相等 ✓）、**编码**（`arm_for` ✓）全部核过 ✓；
而**目标**这一项，我一直用 `init_cred + alias delta + 8` ✗ ⇒ ⇒ **⇒ 若它本应是 `0xffffff8a19880798` 那样**另一类地址** ✗
   ⇒ ⇒ ⇒ **⇒ 那"写不落"就有了最简单、最直接的解释：写去了**别处** ✓✓✓。
⇒ 下一步（唯一）：**读退役 `verify_w2_stage` 的上下文**（⇒ 写请求的 `target` 由谁给出 ✗）⇒ 找出该地址的算式 ✓。
```

## 四、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
