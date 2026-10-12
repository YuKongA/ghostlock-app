# 43499 · 第 40 轮：区间判据落地并**真机验证** ✓✓

## 一、改动（✓，一处 ✓）

```cpp
// main.cpp accept 打印里的 fam 列（第 40 轮 ✓）
(target >= kAliasBase && target < kAliasBase + (1ULL << 38U)) ? 1 : 0     ✓ 39 位线性映射窗 ✓
// 取代第 39 轮的 `(target >> 30) == (kAliasBase >> 30)` ✗（实测会误拦 0xffffff89… ✓）
```

## 二、真机验证（✓✓，一次跑）

```
main: accept step=w1 fam=1 target=0xffffff802adaad88 ✓
main: accept step=w2 fam=1 target=0xffffff8043e49998 ✓ / 0xffffff89215ae198 ✓ / 0xffffff89ab6a1998 ✓
main: accept step=w2 fam=1 target=0xffffff896e271998 ✓ / 0xffffff889d7dab98 ✓ / 0xffffff89af56bd98 ✓
⇒ **无一只被误拦** ✓ ⇒ 判据正确 ✓（只有 `0xffffffc0…` 才会 fam=0 ✗，本窗口未出现 ✓）。
```

## 三、守卫的落地形态（✓，唯一未实现项的最后一步 ✓）

```
把上面这个**已在真机验证过**的布尔值，从"打印列"改为"**越界即不写**" ✓：
  if (!(target >= kAliasBase && target < kAliasBase + (1ULL << 38U))) {
      std::printf("main: target-outside-direct-map step=%s target=0x%llx", …);   // 与退役同义 ✓
      victim.retire_child();
      return "target-outside-direct-map";                                       // 具名 ✓
  }
⇒ 判据：`main: victim uid=0` ✓；辅助：该具名拒绝**偶发**（≈1/8 ✗）而其余轮仍继续 ✓。
```

## 四、门禁（✓）

```
android → RC=0，0 error / 0 warning ✓；check → 109 PASS ✓；lint → 0 ✓
```
