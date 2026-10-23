# KernelSnitch leak 修复证据（2026-10-21）

> 设计：[kernelsnitch-leak-design](kernelsnitch-leak-design.md)（critic 阻断 0）。
> 参考：`origin/main:src/core/kernelsnitch/{kernelsnitch.h,futex_hash.h,scan_bounds.h}`。

## 根因

A301SO：

```text
online=8 possible=8
内核 futex 表 = roundup_pow_of_two(256*8) = 2048
本地旧值   = 256*(online*2) = 4096
```

`__mm_candidate_matches` 用 4096 比较，碰撞对按内核 2048 产生；
2048 同桶的地址在 4096 下可能不同桶，候选全部被拒，`mm_struct` 保持 `-1`。

host 测试固定输出：

```text
pair 2048=1874 4096=3922/1874
```

## 改动

- `futex_hash.h`：新增 `futex_hash_table_size_for()`。
- `kernelsnitch.h`：
  - `possible_cpu_count()`：sysfs `possible` → `_SC_NPROCESSORS_CONF` → online。
  - 用 possible 计算 futex 表；`cpu_cnt` 仍用于日志。
  - `scan_bounds.h` 的 `scan_limit` 限制 coarse/slab 切片。
  - 状态表补 `collisions not found`。
- 新增 `tools/tests/kernelsnitch_hash_test.c`、`perf_probe.c`、host stubs。

## 门禁

```text
./gradlew :app:externalNativeBuildDebug --offline  # 0 warnings
host hash test: pair 2048=1874 4096=3922/1874, rc=0
ghostlock43499 b/B=0
ghostlock43284 b/B=0
```

冷机 A301SO（`/proc/uptime=23.88s` 开跑）：

```text
[spray] futex collisions found +1497ms
[spray] mm_struct leaked=0xffffff8953dc1400 +1556ms
[spray] payload ready +1557ms
prepare_kernel_page ok attempt=1 +1690ms
SELinux permissive
[-] W1 scratch page quarantine failed
rc=1
```

结论：**KernelSnitch leak 已修复**；冷机门禁推进到 W1，新失败点是
`W1 scratch page quarantine failed`，不是 leak。

## 未完成

- `W1 scratch page quarantine failed` 未排查。
- 官方 APK/JNI 冷机门禁仍未跑。
