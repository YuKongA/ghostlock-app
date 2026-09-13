# 6.1 模板说明 / 6.1 Template Guide

模板：[kernel-6.1.template.json](kernel-6.1.template.json)。所有共有字段见 [字段总说明](README.md)，执行参数见 [defaults](../defaults.md)。See the shared references for common fields and tuning.

6.1 默认使用 `tcp_zerocopy`，并保留 `select_stack` 回退；它采用 compact waiter。6.1 primarily uses TCP zerocopy with select-stack fallback and a compact waiter representation.

| 子项 / Item | 模板值 | 作用与影响 / Purpose and impact | 默认理由 / Rationale |
|---|---:|---|---|
| `kernel_major` | 6 | 进入 6.x 通用校验。Selects 6.x validation. | 内核主版本为 6；路线差异由其余字段表达。Route differences stay in profile data. |
| `compact_waiter` | 1 | 选择 6.1 紧凑 waiter，而不是 6.6/6.12 tree waiter。Selects compact layout. | 这是 6.1 路线关键差异，漏填会 miss 或错写。Critical verified distinction. |
| `mm_struct_sz` | 1024 (`0x400`) | TCP 打洞页/heap 对象步长。Heap/object stride. | 使用已验证 SLUB 步长，防止把 BTF 尾部大小误当 allocator stride。Verified allocator stride. |
| `pselect_waiter_shift` | 1 | select 回退的 waiter 栈位移。Fallback stack displacement. | 现有 6.1 编译布局比基准多一个 qword；新镜像仍须反汇编。Existing builds measure one qword; rederive. |
| `kernelsnitch_collisions` | 4 | heap hash 冲突目标。Heap collision target. | 6.x 路线已验证四碰撞，减少 5.x 的额外成本。Verified 6.x balance. |
| `cred_copy_size` | 136 (`0x88`) | credential 复制边界。Credential copy boundary. | 覆盖 6.1 必需字段而不越过模板对象。Verified minimum safe region. |
| `cred_usage_value` | 1 | 初始引用计数。Initial refcount. | 6.x `usage` 布局使用普通初值 1。Matches verified representation. |
| `cred_caps_offset/count/value` | 48 / 5 / -1 | 从 `0x30` 起写五个全 capability 64-bit word。Writes five full capability words. | 匹配 6.x cred capability 布局；`-1` 保留全 1 位模式。Matches layout and all-bits-set mask. |
| `task_*` | JSON 中具体值 | W1/W2/W3 共用的 6.1 `task_struct` 基线。6.1 structural baseline. | 源自已验证 6.1 GKI，但 OEM 配置可改变布局，必须逐镜像 BTF 复核。Verified seed, not universal. |
| 全部 `off_*` | 0 | 固件符号/KASLR 参考地址。Firmware symbol offsets. | 地址绝不跨 release 继承；0 迫使提取。Zero forces extraction. |

模板没有 `requires_shizuku` 时按普通 App 路线处理；只有确认 App seccomp/权限环境不兼容时才显式加入 `1`。Absence means the normal App path; add `1` only with evidence.
