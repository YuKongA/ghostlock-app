# 5.x 模板说明 / 5.x Template Guide

模板：[kernel-5.x.template.json](kernel-5.x.template.json)。共有字段逐项定义见 [模板字段总说明](README.md)，执行参数见 [公共默认值](../defaults.md)。See the shared references for every common field and execution parameter.

5.x 走 `multicast_waiter` 路线，并使用旧 credential 引用镜像；它不能从 6.x 模板推导。5.x uses the multicast-waiter route and legacy credential reference images; 6.x layouts are not transferable.

| 字段 / Field | 模板值 | 作用与影响 / Purpose and impact | 默认理由 / Rationale |
|---|---:|---|---|
| `kernel_major` | 5 | 启用 5.x 强校验和 multicast 必填项；选错会采用错误路线。Enables 5.x validation and route requirements. | 对应本模板唯一支持族。Matches this family. |
| `requires_shizuku` | 1 | 强制 shell UserService，绕开 App seccomp 环境。Forces shell execution. | 当前 5.x 已验证设备需要该身份；新设备必须重新确认。Verified devices require it; recheck new devices. |
| `compact_waiter` | 1 | 选择紧凑 waiter 表示。Selects compact waiter layout. | 5.x 已验证布局；错误会解释错对象。Verified 5.x representation. |
| `mm_struct_sz` | 1024 (`0x400`) | heap 步长/对象布局。Heap stride. | 保留已验证 SLUB 步长，不等同于盲用 BTF `sizeof`。Verified slab stride. |
| `mcast_waiter_off` | 96 | multicast buffer 内 waiter 起点。Waiter start in buffer. | 来自已验证 5.x payload geometry。Verified geometry. |
| `mcast_buffer_size` | 264 | 真实 multicast payload 缓冲区大小。Payload buffer size. | 保证所有真实槽位在界内；过小越界。Bounds the real payload. |
| `mcast_task_offset` | 48 | 真实 task 指针位置。Real task-pointer location. | 与 waiter/lock 成组验证。Part of verified geometry. |
| `mcast_lock_offset` | 56 | 真实 lock 位置。Real lock location. | 错误会锁住或破坏无关内存。Wrong value can corrupt locks. |
| `mcast_fake_lock_offset` | 4608 (`0x1200`) | 伪 lock 在构造区的位置。Fake-lock placement. | 保持旧 payload 的安全间距。Preserves proven spacing. |
| `mcast_fake_task_offset` | 12800 (`0x3200`) | 伪 task 位置。Fake-task placement. | 保持构造对象不重叠。Prevents overlap. |
| `mcast_lock_slots_offset` | 128 (`0x80`) | lock 槽数组起点。Lock-slot array start. | 已验证 payload 布局。Verified layout. |
| `mcast_lock_slot_count` | 12 | 槽数量。Slot count. | 覆盖旧路线所需候选且限制扫描。Enough candidates with a bound. |
| `mcast_lock_slot_stride` | 8 | 槽间距。Slot stride. | 指针宽度为 8 字节。Matches 64-bit pointers. |
| `cred_ref0..3_offset` | 128/136/144/152 | credential 内四个引用指针位置。Four credential reference slots. | 连续 64-bit 槽来自已验证 5.x cred 布局。Verified legacy layout. |
| `cred_ref_count` | 4 | 启用四个引用修复。Enables four reference repairs. | 与上面四槽严格一致。Matches the four slots. |
| `cred_ref0..3_image` | 模板中的负十进制 64-bit 位模式 | 写回目标固件相关引用镜像；错误值可能造成任意引用。Reference pointer images; highly build-sensitive. | 仅作为来源格式示例，适配时必须重测，不能继承。Format examples only; replace for every target. |
| `off_empty_zero_page` | 0 | 5.x 路线需要的 zero page 符号。Required zero-page symbol. | 无跨固件安全默认值。No safe shared value. |
| `off_mcast_fake_bss` | 0 | multicast 伪对象 BSS 地址。Fake-object BSS address. | 无跨固件安全默认值。No safe shared value. |

其余 `off_* = 0` 同样必须填入；task/cred 模板值也必须通过目标 BTF 复核。All remaining zero symbols must be filled, and structural seeds must be verified against target BTF.
