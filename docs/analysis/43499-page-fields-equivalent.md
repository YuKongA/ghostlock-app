# 43499 · 页面字段级比对：一一对应，无缺口（round 152）

## 对照表（退役 `spray.cpp:311-325` ✓ vs 本树 payload_page ✓）

| 退役字段 | 退役取值 | 本树对应物 | 结论 |
| --- | --- | --- | --- |
| `fake_lock` | `payload_base + LOCK_OFF` | `slots.lock` | ✓ |
| `fake_w0` | `payload_base + W0_OFF` | `slots.waiter`（`W0_OFF` 即 `kWaiterOffset` ✓） | ✓ |
| `fake_task` | `payload_base + fake_task_off` | `slots.fake_task` | ✓ |
| `fake_parent/right/left` | layout 的 tuple | 页内元组（`write_layout` ✓） | ✓ |
| `fake_fops` | `payload_base + FOPS_TABLE_OFF` | `fops_table`（payload_page.hpp:43 ✓） | ✓ |

⇒ **字段级无缺口** ✗（这条候选也否证 ✓）。

## 因此尚未做的只剩"**字节级**"

```
字段都在 ✓、语义都在 ✓、offsets 由同一份几何（waiter_off/task_offset/lock_offset/buffer_size ✓）驱动 ✓
⇒ 但要排除"某个字段**写错了位置/写错了值**" ✗，只能把**页面实际字节**与**期望字节**逐字比 ✓：
   期望来源：encode_multicast_waiter（waiter+task/lock ✓）+ PayloadWriteLayout（parent/right/left/fops ✓）
             + 本树的 write_layout（value/parent ✓，已与退役逐字同式 ✓）
   做法：把页面里 6~8 个关键字的**实际值**打印出来（waiter+0x18/0x20/0x28 ✓、lock 槽 ✓、fops 槽 ✓）
        与期望值并列 ✓ ⇒ 任一不符即为缺口 ✓。
```

## 门禁

- android → RC=0，0 警 ✓；check → 108 PASS ✓；lint → 0 ✓。
