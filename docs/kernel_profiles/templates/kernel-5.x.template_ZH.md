# 5.x 内核 profile 模板说明

> English: [kernel-5.x.template.md](kernel-5.x.template.md)

[kernel-5.x.template.conf](kernel-5.x.template.conf) 是 5.15 设备的起点。所有几何值故意留 `null`：未填完的配置保持无效，编辑器和 Run 门槛会标出未完成项。不要为了消除告警把 `null` 改成 `0`。有些字段的 `0` 是有效值，会改变行为。

字段含义、必填矩阵和校验规则在各内核族之间共用。本页只写 5.x 需要改动的部分。

- [Kernel profile 结构](../PROFILE_SCHEMA_ZH.md)：字段分组、必填矩阵、校验
- [公共执行默认值](../defaults_ZH.md)：全部 `execution` 值，均为建议值

## 路线选择

已发布的 5.15 profile 全部声明 `route.multicast_waiter`，并设置 `fallback.to = "none"`。模板同时列出 `tcp_zerocopy` 和 `select_stack` 分支。只保留一个分支，删除其余分支。缺少路线或路线未知时 native 会拒绝。

multicast 分支的已发布取值：

| 字段 | 值 |
|---|---|
| `route.multicast_waiter.waiter_off` | `96` |
| `route.multicast_waiter.buffer_size` | `264` |
| `route.multicast_waiter.task_offset` | `48` |
| `route.multicast_waiter.lock_offset` | `56` |
| `route.multicast_waiter.compact_waiter` | `1` |

multicast 路线还需要其他路线不用的字段：`offset.empty_zero_page`、`kernelsnitch.mm_struct_sz`，以及大于零的 `cred.ref_count`。校验还要求 `cred.copy_size ≥ 0xa0`，且 `waiter_off + lock_offset + 8 ≤ buffer_size`。

## 凭据布局

5.x 要修复凭据的引用字段，因此引用槽参与执行。6.x 把这些字段留 `null`。

| 字段 | 值 |
|---|---|
| `cred.copy_size` | `176` |
| `cred.usage_value` | `256` |
| `cred.caps_offset` | `48` |
| `cred.caps_count` | `3` |
| `cred.caps_value` | `2199023255551` |
| `cred.ref_count` | `4` |
| `cred.ref0_offset` 到 `cred.ref3_offset` | `128`、`136`、`144`、`152` |
| `cred.ref0_image` 到 `cred.ref3_image` | 每台设备各自的镜像偏移 |

`refN_image` 因设备而异，必须和其他偏移取自同一个目标镜像。校验要求每个 `refN_image` 非零，且 `refN_offset + 8` 不超过 `copy_size`。

## KernelSnitch

已发布的 5.15 profile 设置 `kernelsnitch.collisions = 8` 和 `kernelsnitch.mm_struct_sz = 1024`。`mm_struct_sz` 缺失时 native 回退到 `0x500`（`src/core/kernel/constants.hpp`），因此应显式设置该字段，不要依赖回退。

## 数据来源

以上数值取自已发布的 5.15 profile。本页没有重新测量。它们不是跨构建稳定的 ABI：必须用同一目标镜像逐项复核。取值错误会改变目标选择、结构边界或竞争时序，导致失败、死锁、内存损坏、黑屏或重启。native 不校验几何值。Kotlin 的 `validateProfileFields` 把缺失或越界字段记入 `invalidPaths`，并在修好之前禁用 Run。

## 登记 profile

1. 把模板复制到 `app/src/main/assets/kernel_profiles/<release>.conf`。
2. 把 `release` 设为设备 `uname -r` 的完整字符串。文件名和匹配串必须一致。
3. 删除未使用的路线分支，填完每个 `null`。
4. 在 `index.conf` 的 `profiles` 数组加入 `{ release = "<release>", file = "<release>.conf" }`。

不要把执行调优抄进 profile。resolver 会从 `execution-*.conf` 加载。
