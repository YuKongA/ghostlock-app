# 43499 · 第 97 轮：**清单第①步完成**（作用域提升，无回归）✓✓

## 一、改动（✓，小且语义不变 ✓）

```cpp
// main.cpp 修复轮之前（现为外层声明 ✓）：
cve49::steps::PrepareReport prepared2{};                              // 原在 if 内 ⇒ 提出 ✓
std::uint64_t repair_base = 0U;                                       // 原在 else 内 ⇒ 提出 ✓
std::vector<std::uint8_t> repair_image(kOrder3 * kChunks, 0U);        // 原在 else 内 ⇒ 提出 ✓
if (repair_target != 0U) {
    const char *repair_refusal = nullptr;
    if (!cve49::steps::run_prepare(prepare, prepared2, &repair_refusal) || …) { …refused… }
    else { repair_base = prepared2.base + payload_delta; …（其余不变 ✓）}
}
```

## 二、判据（✓✓）

```
门禁：`android` RC=0，**0 error / 0 warning** ✓；`check` **109 PASS** ✓；
真机（无回归 ✓）：`repair-prepare-refused`/`repair-slot-refused` 计数 **0** ✓；
  `main: repair-round step=w1 target=0xffff800000090108` ✓；`route status=0/0 clean=1/1 attempts=16` ✓；
  `main: mcast ghost disarm ret=-1 errno=110` ✓；`main: victim uid=2000` ✗（一如往常 ✓）。
```

## 三、下一步（✓，现在是**纯语句移动** ✓）

```
把这三段（`run_prepare` 调用 ✓ / `repair_base` 赋值 ✓ / 建页那几行 ✓，即现行"if(repair_target) 内的 else 体"）
**整段上移**到主路由 `stage_refusal = run_window_stage_with_attempt(` **之前** ✓ ⇒ ⇒ 这就是退役的"预建" ✓；
⇒ 因声明已在外层 ✓ ⇒ 上移时**不再动作用域** ✓ ⇒ 属机械操作 ✓；
⇒ 之后：修复轮只留 `activate` + send + route ✓ + 出口 `discard` ✓ + 该次 5000 µs ✓（第 90 轮 ✓）。
判据：真机出现归档同形 `fast repair payload prebuilt` ✓ / `firing prebuilt init_cred+8 repair` ✓ ⇒ **再看 `uid`** ✓。
```

## 四、门禁（✓，全绿 ✓）

```
check → **109 PASS** ✓；android → RC=0，**0 error / 0 warning** ✓；lint → 0 ✓
```
