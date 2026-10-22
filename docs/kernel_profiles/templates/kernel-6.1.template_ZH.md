# 6.1 内核 profile 模板说明

> English: [kernel-6.1.template.md](kernel-6.1.template.md)

[kernel-6.1.template.conf](kernel-6.1.template.conf) 是 6.1 设备的起点。所有几何值故意留 `null`：未填完的配置保持无效，编辑器和 Run 门槛会标出未完成项。不要为了消除告警把 `null` 改成 `0`。有些字段的 `0` 是有效值，会改变行为。

字段含义、必填矩阵和校验规则在各内核族之间共用。本页只写 6.1 需要改动的部分。

- [Kernel profile 结构](../PROFILE_SCHEMA_ZH.md)：字段分组、必填矩阵、校验
- [公共执行默认值](../defaults_ZH.md)：全部 `execution` 值，均为建议值

## 路线选择

已发布的 6.1 profile 全部声明 `route.tcp_zerocopy`，并设置 `compact_waiter = 1`。其中 20 个还声明 select 栈回退：

```hocon
fallback {
  to = "select_stack"
  route {
    select_stack { waiter_shift = 1 }
  }
}
```

另有 3 个已发布 profile 设置 `to = "none"`。目前只实现了 `tcp_zerocopy` 回退到 `select_stack` 这一种，不要声明其他目标。只保留一个 `route` 分支，删除其余分支。

## 凭据布局

模板把凭据块直接写在文件里。多数已发布的 6.1 profile 改用 `include "credential-6x.conf"`，这是 6.x 共用的模板；它把 5.x 的引用槽留为 `null`，因为 6.x 不修复这些槽。两种写法都可用。

| 字段 | 值 |
|---|---|
| `cred.copy_size` | `136` |
| `cred.usage_value` | `1` |
| `cred.caps_offset` | `48` |
| `cred.caps_count` | `5` |
| `cred.caps_value` | `-1` |
| `cred.ref_count` | `null` |

`caps_offset` 是唯一会变的取值，有一个已发布 profile 把它覆盖为 `40`。应取自目标镜像，不要假定为 `48`。

资源目录里两个自包含的 6.1 profile 也把同一块直接写进文件，而不是 include `credential-6x.conf`。

## KernelSnitch

已发布的 6.1 profile 都设置 `kernelsnitch.collisions = 4` 和 `kernelsnitch.mm_struct_sz = 1024`。多数 profile 从 `kernelsnitch-6x.conf` 取得前者，再在本地覆盖大小。该 include 中 `mm_struct_sz = null`，native 回退值是 `0x500`（`src/core/kernel/constants.hpp`），因此 6.1 布局必须写这个覆盖。模板没有 `mm_struct_sz` 行，适配时需要补上。

## 数据来源

以上数值取自已发布的 6.1 profile。本页没有重新测量。它们不是跨构建稳定的 ABI：必须用同一目标镜像逐项复核。取值错误会改变目标选择、结构边界或竞争时序，导致失败、死锁、内存损坏、黑屏或重启。native 不校验几何值。Kotlin 的 `validateProfileFields` 把缺失或越界字段记入 `invalidPaths`，并在修好之前禁用 Run。

## 登记 profile

1. 把模板复制到 `app/src/main/assets/kernel_profiles/<release>.conf`。
2. 把 `release` 设为设备 `uname -r` 的完整字符串。文件名和匹配串必须一致。
3. 加入 `include "credential-6x.conf"` 和 `include "kernelsnitch-6x.conf"`，用目标需要的本地覆盖替换内联的 `cred` 与 `kernelsnitch` 块，删除未使用的路线分支，填完每个 `null`。
4. 在 `index.conf` 的 `profiles` 数组加入 `{ release = "<release>", file = "<release>.conf" }`。

不要把执行调优抄进 profile。resolver 会从 `execution-*.conf` 加载。
