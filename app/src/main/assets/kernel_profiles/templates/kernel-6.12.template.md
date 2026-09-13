# 6.12 模板说明 / 6.12 Template Guide

模板：[kernel-6.12.template.json](kernel-6.12.template.json)。所有共有字段见 [字段总说明](README.md)，执行参数见 [defaults](../defaults.md)。See shared field and tuning references.

6.12 同样走 tree waiter/`select_stack`，但 `task_struct` 相对 6.6 明显增长，禁止复用 6.6 偏移。6.12 uses the same route family but has a larger task layout; 6.6 offsets are invalid.

| 子项 / Item | 模板值 | 作用与影响 / Purpose and impact | 默认理由 / Rationale |
|---|---:|---|---|
| `kernel_major` | 6 | 选择 6.x 通用验证。Selects 6.x validation. | schema 当前按 major 表达，6.12 差异由字段值体现。Differences remain data-driven. |
| `compact_waiter` | 缺省 0 | 使用 tree waiter。Uses tree waiter. | 当前 6.12 已验证路线。Verified representation. |
| `pselect_waiter_shift` | 0 | waiter 位于反汇编基准位置。Waiter is at the derived base position. | 现有 6.12 编译链测得零位移；仍须逐镜像反汇编。Measured seed, still rederive. |
| `kernelsnitch_collisions` | 4 | heap hash 碰撞目标。Heap collision target. | 延续已验证 6.x 平衡值。Preserves proven 6.x balance. |
| `cred_copy_size` | 136 (`0x88`) | credential 复制范围。Credential copy range. | 当前 6.12 所需 cred 字段仍在该边界内。Verified required range. |
| `cred_usage_value` | 1 | credential 引用计数初值。Credential refcount image. | 与已验证 6.12 表示一致。Matches verified representation. |
| `cred_caps_offset/count/value` | 48 / 5 / -1 | 写入全部 capability 集。Writes all capability sets. | 维持 6.x cred 布局和全 1 位图。Matches 6.x layout. |
| `task_*` | JSON 中具体值 | 6.12 `task_struct` 基线，包括更靠后的 PI、cred、seccomp 字段。6.12 structural baseline. | 来自已验证 6.12 GKI；与 6.6 数值不同，必须整组复核。Verified 6.12 seed; validate as a group. |
| 全部 `off_*` | 0 | 目标 release 的符号/KASLR 参考。Target-release symbols. | 即便小版本相同，构建 ID 不同也可能变化，因此无共享默认。No safe cross-build default. |

`task_*` 中任一项改变时应重新检查全部同组字段，而不是只修复触发失败的单项。If any task offset differs, revalidate the entire group rather than patching only the observed failure.
