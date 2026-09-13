# 6.6 模板说明 / 6.6 Template Guide

模板：[kernel-6.6.template.json](kernel-6.6.template.json)。所有共有字段见 [字段总说明](README.md)，执行参数见 [defaults](../defaults.md)。See shared field and tuning references.

6.6 使用 tree waiter 和 `select_stack` 路线。6.6 uses the tree-waiter representation and select-stack route.

| 子项 / Item | 模板值 | 作用与影响 / Purpose and impact | 默认理由 / Rationale |
|---|---:|---|---|
| `kernel_major` | 6 | 选择 6.x profile 验证。Selects 6.x validation. | 与目标主版本一致。Matches target family. |
| `compact_waiter` | 缺省 0 | 选择 rb/tree waiter。Selects tree waiter. | 6.6 已验证结构不是 6.1 compact 形式；不要显式写 1。Verified non-compact layout. |
| `pselect_waiter_shift` | -2 | 定位 select 栈中的 waiter。Locates waiter in select stack. | 当前 6.6 编译链测得基准前移两个 qword；必须对目标镜像复核。Measured seed requiring rederivation. |
| `kernelsnitch_collisions` | 4 | heap hash 碰撞目标。Heap collision target. | 当前 6.6 可靠性/成本平衡值。Proven reliability/cost balance. |
| `cred_copy_size` | 136 (`0x88`) | credential 复制边界。Credential copy boundary. | 覆盖所需 6.6 字段且限制越界。Verified bounded region. |
| `cred_usage_value` | 1 | credential 引用计数初值。Credential refcount image. | 对应当前 6.6 cred 表示。Matches verified layout. |
| `cred_caps_offset/count/value` | 48 / 5 / -1 | 写入五组全 capability。Writes five full capability words. | 与 6.6 credential 模板一致。Matches 6.6 layout. |
| `task_*` | JSON 中具体值 | 6.6 `task_struct` 偏移基线。6.6 task layout seed. | 来自已验证 GKI；OEM backport/config 可改变布局，必须 BTF 复核。Verified seed, not a guarantee. |
| 全部 `off_*` | 0 | 固件专属符号地址。Firmware-specific symbols. | 不能从同为 6.6 的另一 release 复制。Never transferable across releases. |

若目标固件只能在 shell 身份运行，显式添加 `requires_shizuku: 1`；否则保持字段缺省。Add the flag only when shell execution is required and tested.
