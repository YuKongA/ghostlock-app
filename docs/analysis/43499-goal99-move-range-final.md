# 43499 · 第 99 轮：上移范围**完全确定**（`:1192-1223` ⇒ 到 `:1168` 之前）✓✓

## 一、现行布局（✓，逐行 ✓）

```
:1168  主路由：`const char *stage_refusal = cve49::steps::run_window_stage_with_attempt(` ✓
:1192  `std::uint64_t repair_target = 0U;`                          ← 在主路由**之后** ✗
:1194  `repair_target = kAliasBase + init_cred_image + alias_image_delta + 8U;`（w2 ✓）← 之后 ✗
:1198  `repair_target = prepared.base + route_values.buffer_size;`（w1 ✓）← 之后 ✗
:1220  `cve49::steps::PrepareReport prepared2{};`                    ← 之后 ✗
:1223  `bool repair_prebuilt = false;`                              ← 之后 ✗
:1224  `if (repair_target != 0U && !repair_prebuilt) {`               ← 之后 ✗
```

## 二、结论（✓✓，范围与依赖 ✓）

```
**上移范围 = `:1192-1223`**（`repair_target` 的声明与两式计算 ✓ + `prepared2` / `repair_base` /
`repair_image` / `repair_prebuilt` 的声明 ✓）⇒ **到 `:1168` 之前** ✓；
⇒ 而它们的依赖——`step` ✓ / `prepared.base` ✓ / `route_values.buffer_size` ✓ / `init_cred_image` ✓ /
`alias_image_delta` ✓ / `kAliasBase` ✓ ——**全部在主路由之前可得** ✓ ⇒ ⇒ **⇒ 上移在依赖上无阻** ✓；
⇒ 此即第 98 轮编译器报错所指（"声明之前" ✗）的正确解法 ✓：**连同声明一起上移** ✓，不是插入副本 ✓。
```

## 三、执行清单（✓，机械操作 ✓）

```
① 上移 `:1192-1223` ⇒ 到 `:1168` 之前 ✓；② 再把「prepare + 建页」的那段（现行 `:1224` 的 `if` 体内 ✓）
   一并上移 ✓，使修复轮只剩 `activate` + send + route ✓；③ 出口（含早退）补 `discard` ✓；
④ 该次路由的进入延迟取 5000 µs ✓（第 90 轮：取值走 profile/wire ✓）；
⑤ 门禁三线**同批** ✓（★ 含 host ✓ —— 第 85 轮教训 ✓）；定位**一律用内容锚点** ✓（第 91 轮教训 ✓）。
判据：真机出现归档同形 `fast repair payload prebuilt` ✓ / `firing prebuilt init_cred+8 repair` ✓ ⇒ **再看 `uid`** ✓。
```

## 四、门禁（✓，全绿 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
