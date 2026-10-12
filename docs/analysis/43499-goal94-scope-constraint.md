# 43499 · 第 94 轮：搬移的**真实约束**（跨作用域，非行移动）✓✓

## 一、逐字读到的修复块（✓，`:1214-1253` ✓）

```cpp
if (repair_target != 0U) {                                 // :1214
    cve49::steps::PrepareReport prepared2{};               // :1215  ← 局部 ✗
    const char *repair_refusal = nullptr;                  // :1216
    if (!run_prepare(prepare, prepared2, &repair_refusal) || …) {   // :1217 ← **提前点** ✓
        printf("main: repair-prepare-refused %s", …);      // :1219
    } else {
        const std::uint64_t repair_base = prepared2.base + payload_delta;   // :1222  ← 局部 ✗
        std::vector<std::uint8_t> repair_image(kOrder3 * kChunks, 0U);      // :1223  ← 局部 ✗
        … write_layout(:1232) / build_payload_page(:1238) / memcpy(:1241) …
        if (!repair_slot.stash(…) || !repair_slot.activate(…)) { …回退… }   // :1244-1249
        send_bytes(prepared2.shaping_fd, repair_page, repair_length);       // :1250-1253（**已用槽** ✓）
    }
}
```

## 二、真实约束（✓✓✓）

```
块内产生三个**局部量**：`prepared2` ✓ / `repair_base` ✓ / `repair_image` ✓（都在 `else {` 内 ✓）；
而"预建"要求它们**在主路由之前**就存在 ✓ ⇒ ⇒ **⇒ 必须把这三点*提升到外层作用域*** ✗✓；
⇒ ⇒ ⇒ **⇒ 结论：这是一次"搬移 + 作用域提升"** ✗ ⇒ ⇒ **⇒ 必须一次做完（搬移 + 提升 + 出口 discard）
   并同批跑满三门禁** ✓（⇒ **不得**拆成"先加后改"✗ —— 第 93 轮已证那条路会被回退吃掉 ✓）；
⇒ 另：`repair_target` 的计算（`:1210-1213` 一带 ✓）也须随之**上移** ✗（⇒ 它依赖 `step` 与 `prepared.base` ✓，两者在主路由前都可得 ✓）。
```

## 三、执行检查表（✓，接手直接照做 ✓）

```
① 上移：`repair_target` 计算 ✓ + `prepared2`/`repair_base`/`repair_image` 的**声明与填充** ✓ ⇒ 到主路由
   `stage_refusal = run_window_stage_with_attempt(` ✓ 之前；② 修复轮改为**只用** `activate` + send + route ✓；
③ 出口（含早退）补 `discard` ✓；④ 门禁三线**同批** ✓（含 host ✓）；`grep -n` 定位**一律用内容锚点** ✓。
判据：真机出现归档同形 `fast repair payload prebuilt` ✓ / `firing prebuilt init_cred+8 repair` ✓ ⇒ **再看 `uid`** ✓。
```

## 四、门禁（✓，全绿 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
