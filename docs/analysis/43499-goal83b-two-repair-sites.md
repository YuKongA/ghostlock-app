# 43499 · 第 83 轮（补）：本树有**两处**修复，78 次读数全来自第一处 ✓✓

## 一、结构（✓✓✓，逐行 ✓）

```cpp
// ① `main.cpp:836-843` —— 目标**写死**为 init_cred + 8 ✗（W2 专用 ✓）
:837  repair_request.target = kAliasBase + init_cred_image + alias_image_delta + 8U;   // ← 写死 ✗
:839  repair_request.mode = WriteMode::Zero;
:843  std::printf("main: repair target=0x%llx mode=%d preserve=%d pc=0x%llx right=0x%llx left=0x%llx", …);
// ② `main.cpp:1143-1207` —— 用 **`repair_target` 变量** ✓（含 w1 的 `prepared.base + buffer_size` ✓）
```

## 二、因此（✓✓，收回"从未执行"的推断 ✓）

```
三份日志里 78 次 `main: repair target=**0xffffff802abfd590**` ⇒ **全部来自 ①** ✓（⇒ 它的目标写死 ✓，
所以"78 次同值"是**必然**的 ✗，不构成"w1 修复没跑"的证据 ✗）；
⇒ ⇒ **⇒ 第 83 轮前半的"w1 修复从未执行"推断** ⇒ **收回** ✗ ✓（⇒ ② 的读数不在这 78 行里 ✓）。
```

## 三、仍未定的一项（✓，一处读即可 ✓）

```
**② 有没有自己的打印** ✗ —— 若有（例 `main: repair-round target=…` ✓）⇒ 说明 w1 的修复**跑过** ✓；
若**没有** ⇒ 那么 ② 的行为在真机上**完全不可见** ✗ ⇒ ⇒ **⇒ 先给它补一行打印** ✓（一行 ✓），
再据读数判"它是否按 `w1_scratch_repair_attempts` 重试"✓ ⇒ ⇒ 这与第 81 轮的 W1b 清单**合流** ✓。
```

## 四、下一步（✓）

```
① 读 `main.cpp:1143-1207` 里**是否有打印** ✗（一处 grep ✓）；② 无则补一行（`repair-round step=%s target=0x%llx` ✓）；
③ 真机对照：若 w1 的修复出现且目标 = `base + buffer_size` ✓ ⇒ 再按退役同形补"轮数循环"与两个归档同形字符串 ✓；
④ 判据不变：任一轮 `main: victim uid=0` ✓。
```

## 五、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 error / 0 warning ✓；lint → 0 ✓
```
