# 43499 · 更正：页面字节**是对的**（round 153）—— 我比错了分支

## 更正（上一份文档的"错位"结论作废 ✗）

```cpp
// 退役 payload_builder.cpp:34-45（encode_compact_waiter）**两支**：
if (request.preserve_child) {                     // ★ 我上次漏读的一支
    waiter+0x18 ← layout.right
    waiter+0x20 ← 0
    waiter+0x28 ← request.target                  // ⇒ (right, 0, target)
} else {
    waiter+0x18 ← layout.parent ; +0x20 ← layout.right ; +0x28 ← layout.left
}
```

## 本树页面实际（真机逐字 ✓，用**文档的真实偏移** 0x18/0x20/0x28 ✓）

```
w1: pc=0x18 -> 0xffff800000070100 | right=0x20 -> 0x0 | left=0x28 -> 0xffffff802adaad88
    （= right ✓ / 0 ✓ / target ✓  ⇒ **preserve 支逐字一致** ✓）
w2: 0x18 -> 0xffffff802abfd588   | 0x20 -> 0x0 | 0x28 -> 0xffffff8991b07398
    （w2 的 right = init_cred 别名 ✓；target = child+0x798 ✓）
```

⇒ ⇒ **页面字节级比对：通过** ✓（我上一份文档的"错位"是用错了期望分支 ✗ ⇒ 作废 ✓）。

## 于是"未能对照"的清单再少一项

| 层级 | 状态 |
| --- | --- |
| 代码 / 常数 / 几何 / 数值 / 字段 / 判据语义 | 全部对齐 ✓ |
| **页面字节** | **对齐** ✓（本轮 ✓，按 preserve 支） |
| 结果 | `selinux enforce=1` ✗、`uid=2000` ✗ 仍不变 ✗ |

## 教训（登记 ✓）

- 同一函数有**两支**时，比对必须**逐支**比 ✓（我这次只读到一支 ✗）；
- 但"打印实际值 + 并列期望值"这个做法**有效** ✓ —— 正是它把两支的差异逼了出来 ✓。
