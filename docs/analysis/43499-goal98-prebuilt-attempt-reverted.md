# 43499 · 第 98 轮：预建尝试被编译器拦下 ⇒ 当场回退（教训与证据）✓

## 一、做了什么（✓，如实 ✓）

```
① 在**主路由之前**插入一段"预建"（prepare + 建页 + `repair_prebuilt = true` ✓）；
② 旧处加守卫 `if (repair_target != 0U && !repair_prebuilt)` ✓ + 声明 `bool repair_prebuilt = false;` ✓；
③ 编译：**`android` RC=2** ✗ —— `error: use of undeclared identifier 'repair_target'` / 'prepared2' ✗✗。
```

## 二、为什么（✓✓，被编译器当场证实 ✓）

```
插入点（主路由 `stage_refusal = run_window_stage_with_attempt(` ✓）位于 `repair_target` / `prepared2` /
`repair_base` / `repair_image` 的**声明之前** ✗ ⇒ ⇒ **⇒ 只"插入副本"必然编译不过** ✗；
⇒ ⇒ ⇒ **⇒ 这正是第 94 轮清单写明的约束** ✓（⇒ **必须"连同声明整段上移"** ✓，而非插入 ✓）——
   我本轮的操作**违反了它** ✗ ⇒ 编译器**当场拦下** ✓（⇒ 这就是"守卫须能失败"的另一面 ✓：**约束被违反时会立刻叫** ✓）。
```

## 三、回退（✓✓，干净 ✓）

```
① 先试 `if (false)` 占位 ✗ —— **无效** ✓（`if (false)` 内仍要编译 ✗ ⇒ 未声明标识符照样报错 ✓）；
② 遂**按插入时的逐字文本整段删除** ✓ ⇒ 换成一条**就地注释** ✓：
   写明"只插一段会编译不过 ✗ ⇒ 正确做法 = 连同声明整段上移 ✓（第 94 轮清单 ✓）" ✓；
⇒ 门禁复绿 ✓：`android` RC=0，**0 error / 0 warning** ✓；`check` **109 PASS** ✓；`lint` 0 ✓。
```

## 四、下一步（✓，方式已被本轮钉正 ✓）

```
**整段上移**（不是插入 ✗）：范围 = 「`repair_target` 的计算」✓ + 「三个声明」✓ + 「prepare + 建页」✓ ⇒
  到主路由**之前** ✓；⇒ 旧处只留 `activate` + send + route ✓ + 出口 `discard` ✓ + 该次 5000 µs ✓。
⇒ 提示：上移后 `repair_target` 的计算会依赖 `step` 与 `prepared.base` ✓ ⇒ 二者在主路由前**都可得** ✓（第 94 轮结论 ✓）。
判据：真机出现归档同形 `fast repair payload prebuilt` ✓ / `firing prebuilt init_cred+8 repair` ✓ ⇒ **再看 `uid`** ✓。
```

## 五、门禁（✓，全绿 ✓）

```
check → **109 PASS** ✓；android → RC=0，**0 error / 0 warning** ✓；lint → 0 ✓
```
