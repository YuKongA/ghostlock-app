# 43499 · 第 20 轮（「瓶颈找旧代码」）：一号嫌疑 = waiter 首字缺 RED ✓✓✓

## 一、旧代码逐字（✓，`primitives.cpp:157-159` ✓）

```
/* Both transports write *(target) := value through the erase left-only
 * relink: waiter words are {pc = value, right = 0, left = target} and
 * the node is **RED** so no color fixup runs. leaf=1 is the value=0 payload. */
⇒ ① 元组 = {pc=value, right=0, left=target} ✓（与已实现一致 ✓）；
② **节点必须 RED** ✗ —— 否则 **color fixup 会跑** ⇒ 擦除式左向重链不成立 ⇒ 写落不到目标 ✗；
③ **`leaf=1` 才是 value=0 的载荷** ✓（即 preserve=false ⇒ 才对应零值 ✓）。
```

## 二、退役的 waiter 首字（✓，`spray.cpp:355-366` 逐字 ✓）

```
W0_OFF + 0x00 = **1**         /* tree_entry.rb_parent_color ⇒ 低位=RED ✓ */
W0_OFF + 0x08 = 0             /* rb_right */
W0_OFF + 0x10 = 0             /* rb_left */
W0_OFF + 0x30 = waiter_task   /* task */
W0_OFF + 0x38 = fake_lock     /* lock */
W0_OFF + 0x40 = 0             /* wake_state */
```

## 三、本树现状（✓，**第 21 轮已查明：RED 位本来就设了 ✓**）

```cpp
// payload_page.cpp:102（逐字 ✓）
put64(page, waiter_at + inputs.waiter.head_word, **1U**);   ✓ = 退役 `W0_OFF + 0x00 = 1` ✓
⇒ ⇒ **RED 嫌疑排除** ✗（我此前只在 `memory/` 里 grep "RED|color|colour" ✗ —— 而本树把这一位
   写在"head_word = 1"这一句里 ✓，名字不同、语义相同 ✓）。
⇒ 顺带核对：`head_next`/`head_prev` = 0 ✓（= 退役 `rb_right`/`rb_left` = 0 ✓）语义一致 ✓。
```

## 三-旧、第 20 轮的原判断（已被上节更正 ✓）

```
`grep RED|color|colour` 在 `memory/` 下 **0 命中** ✗；
`payload_page.cpp:50` 只用 `inputs.slots.waiter` 定位 ✓，未见任何"最低位置 1"的写入 ✗。
⇒ ⇒ **一号嫌疑成立**：首字（`+0x00`）若不为 1 ⇒ 内核按 BLACK 处理 ⇒ 会走 color fixup ⇒
   与"stamp 落地（99 ✓）而写不生效 ✗"的现象**完全吻合** ✓。
```

## 四、下一步（✓，一处小改 + 一条判据 ✓）

1. 读 `payload_page.cpp` 的 waiter 编码段 ✓，确认 `+0x00` 现写什么 ✗；
2. 若不为 1 ⇒ 改为 **1** ✓（注释引 `spray.cpp:355` 逐字 ✓）；
3. 判据：真机 `main: victim uid=0` ✓（或先看 `step result w2 => won` 是否不再 `w2-not-rooted` ✓）。

## 五、门禁（✓，本轮未改码 ✓）

```
check → 109 PASS ✓；android → 0 警 ✓；lint → 0 ✓
```
