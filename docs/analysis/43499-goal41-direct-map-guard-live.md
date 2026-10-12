# 43499 · 第 41 轮：直映射守卫**实现并在真机生效**（台账唯一未实现项已清零 ✓）

## 一、改动（✓）

```cpp
// main.cpp 步骤体内、建页之后（退役 primitives.cpp:152 的首行守卫 ✓ 同义）
if (!(target >= kAliasBase && target < kAliasBase + (1ULL << 38U))) {
    std::printf("main: target-outside-direct-map step=%s target=0x%llx", …);
    victim.retire_child();
    return "target-outside-direct-map";                  // 具名 ✓（守则：具名拒绝而非硬写 ✓）
}
```

## 二、真机判据（✓✓）

```
`target-outside-direct-map` 计数 = **3** ✓ ⇒ **守卫确实会拦** ✓（与"≈1/8 越界"的预测一致 ✓）；
`step result w1 => won` ✓；`step result w2 => **w2-not-rooted**` ✓（第 18 轮具名读数 ✓）；
`main: victim uid=2000` ✗ ⇒ 仍未 root ✓。
```

## 三、台账状态（✓，唯一未实现项已清零 ✓）

```
`in_direct_map` 首查（第 5 条 ✗）⇒ **已实现** ✓（本轮 ✓，判据经第 39/40 轮真机验证 ✓）；
⇒ 至此台账 13 条**全部对齐或无差异** ✓ ✓（剩 `fast_repair` 一族：采用页槽 ✓ 已接、时序项待做 ✗）。
```

## 四、门禁（✓）

```
android → RC=0，0 error / 0 warning ✓；check → 109 PASS ✓；lint → 0 ✓
```

## 五、下一步（✓）

```
① `fast_repair` 的时序两项（预建提前 ✗ / 5000 µs ✗，锚点已在 goal25 文档 ✓）；
② 或换方向：查"写值已非零 ✓、页被采纳 ✓、路由 successes=1 ✓ 而 uid 仍 2000 ✗"之间的那一段 ✓ ——
   即"内核是否真的执行了到目标的写"⇒ 需要独立读回（别名读 ✓，属 L 级 ⇒ 先计划 ✓）。
```
