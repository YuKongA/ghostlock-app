# KernelSnitch leak 设计：按 origin/main C++ 版修正哈希表尺寸（2026-10-21）

> 触发：冷机 standalone 43499 在 A301SO 上 4 次 `KernelSnitch mm_struct leak failed`。
> 参考：`origin/main:src/core/kernelsnitch/{kernelsnitch.h,futex_hash.h,scan_bounds.h}`。
> 目标：只修 leak 的根因与扫描边界，不改攻击调用序列。

## 证据

设备 A301SO / kernel 5.15.189：

```text
getconf _NPROCESSORS_ONLN = 8
getconf _NPROCESSORS_CONF = 8
/sys/devices/system/cpu/possible = 0-7
```

冷机日志：

```text
[spray] mm spray + kernelsnitch ready (cpu=8)
[spray] futex collisions found
[spray] mm_struct leaked=0xffffffffffffffff
KernelSnitch mm_struct leak failed
```

本地：

```c
ks->cpu_cnt = sysconf(_SC_NPROCESSORS_ONLN) * 2;   // 16
ks->futex_hash_table_size = 256 * ks->cpu_cnt;      // 4096
ks->futex_hashsize = 4096;
```

Linux futex 表：

```text
roundup_pow_of_two(256 * num_possible_cpus())
= roundup_pow_of_two(256 * 8)
= 2048
```

因此：

- 内核用 **2048** 分桶；
- 用户态 `__mm_candidate_matches` 用 **4096** 比较；
- 按内核时序找到的 collision 对，在 4096 哈希下不保证同桶；
- 没有任何候选能通过全部比较，`ks->mm_struct` 保持 `-1`。
- origin/main 的 C++ 版已改为 `futex_hash_table_size_for(possible_cpu_cnt)`，并对
  `identity_diff`/扫描切片使用 `scan_limit`。

## 改动

### 1. futex_hash.h

新增 C 版：

```c
static inline size_t futex_hash_table_size_for(size_t cpu_count) {
  size_t size = (cpu_count ? cpu_count : 1) * 256;
  size_t rounded = 1;
  while (rounded < size) rounded <<= 1;
  return rounded;
}
```

### 2. kernelsnitch_setup

```c
long online_l = sysconf(_SC_NPROCESSORS_ONLN);
long conf_l = sysconf(_SC_NPROCESSORS_CONF);
size_t online = online_l > 0 ? (size_t)online_l : 1;
size_t possible = possible_cpu_count(conf_l, online); /* sysfs -> CONF -> online */
ks->cpu_cnt = online * 2;                  /* log/thread display only */
ks->futex_hash_table_size = futex_hash_table_size_for(possible);
ks->futex_hashsize = (uint32_t)ks->futex_hash_table_size;
ks->total_futexes = ks->futex_hash_table_size * ks->collisions * MULITPLE;
```

`static size_t possible_cpu_count(long conf_l, size_t online)`：

1. 读 `/sys/devices/system/cpu/possible`，解析 `0-7`/逗号 cpumask 位数；
2. 失败用 `conf_l > 0`；
3. 再失败用 `online`；
4. 全失败用 1。

`conf_l` 在调用点被消费，`-Werror` 下无 unused 变量。

不得把 `sysconf` 的 -1 直接转 `size_t`。

### 3. scan_bounds

新增：

```c
static inline size_t scan_limit(size_t start, size_t span, size_t end) {
  if (start >= end || span >= end - start) return end;
  return start + span;
}
```

`__mm_leak`：

- `slab_end = scan_limit(coarse_addr, COARSE_SZ, range->end)`
- `slab_limit = scan_limit(slab_addr, mm_slab_sz, slab_end)`
- 循环条件用 `slab_addr < slab_end` / `candidate < slab_limit`

`__run_mm_leak_pass`：

- `ceiling = IDENTITY_END`（本地没有进程级 `g_direct_map_end`；`DIRECT_MAP_END` >=
  identity end，取较小值等价）
- `range.end = min(range.end, ceiling)`
- 日志记录 `possible/conf/online/table_size`（verbose 或一次性 info）。

### 4. state 字符串

按 origin 修复：加入 `"collisions not found"`，避免后续状态错位。

## 不改

- `kernelsnitch_find_collisions` 的时序/线程/碰撞逻辑。
- `kernelsnitch_bruteforce` 的调用点。
- 43499 攻击调用序列。
- 43284。
- 不引入全局。

## 风险

- `_SC_NPROCESSORS_CONF` 在容器/受限进程可能返回 0；回退 online。
- 表尺寸变小会减少 `total_futexes` 内存和 probe 数；默认路径值需由设备测试确认。
- `scan_limit` 只防越界，不改变有效范围。
- 若仍失败，必须先用 `tools/tests/perf_probe.c` 采集 `perf_event_open` 的 fd/errno，
  并抓 `adb logcat -d` 的 `avc: denied`；证据明确前不继续猜 profile。

## 门禁

### Host 哈希/边界测试

已新增 `app/src/main/cpp/glgeneral/kernelsnitch/scan_bounds.h` 与
`tools/tests/kernelsnitch_hash_test.c`。

测试：

1. `scan_limit(0,10,100)==10`、`scan_limit(95,10,100)==100`、
   `scan_limit(100,10,100)==100`、`scan_limit(SIZE_MAX,10,SIZE_MAX)==SIZE_MAX`；
2. 在 0x100000000..0x100200000 找一对地址：
   2048 同桶、4096 不同桶；
3. 实跑输出：

```text
pair 2048=1874 4096=3922/1874
```

命令：

```text
clang -std=gnu11 -Wall -Wextra -Werror \
  -include tools/tests/stubs/cpu_set.h \
  -I app/src/main/cpp/glgeneral/kernelsnitch \
  -I tools/tests/stubs \
  tools/tests/kernelsnitch_hash_test.c \
  -o /tmp/kernelsnitch_hash_test
/tmp/kernelsnitch_hash_test  # rc=0
```

### Cold A301SO standalone

```text
# 构建
CC=aarch64-linux-android24-clang
$CC -std=gnu11 -O2 -fPIE -pie -DTARGET_CONFIG_H="target.h" \
  -I app/src/main/cpp/glk43499 -I app/src/main/cpp/gl43499/kernels \
  -I app/src/main/cpp/glgeneral -I app/src/main/cpp/glk_log \
  main.c fops.c util.c offsets_json.c glk_session.c glk_log.c glk43499_api.c \
  -llog -o /tmp/glk43499
adb push /tmp/glk43499 /data/local/tmp/glk43499
adb reboot
# boot_completed=1 后执行
adb shell /data/local/tmp/glk43499 > /tmp/ks-fix-cold.log 2>&1
```

PASS 判据：

- 不再出现 `mm_struct leaked=0xffffffffffffffff`；
- 至少进入 `=== W1: SELinux ===`/`W1: SELinux attempt`；
- 设备不 panic。

`tools/tests/perf_probe.c` 已存在。构建/运行：

```text
CC=/Users/nickji/Library/Android/sdk/ndk/30.0.16248370/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android24-clang
$CC -std=gnu11 tools/tests/perf_probe.c -o /tmp/perf_probe
adb push /tmp/perf_probe /data/local/tmp/perf_probe
adb shell chmod 755 /data/local/tmp/perf_probe
adb shell /data/local/tmp/perf_probe
adb logcat -d | grep 'avc: denied' | tail
```

冷机前已实测：

```text
perf_probe fd=3 errno=0 (Success)
```

因此 perf 本身可用；若修复后仍 leak，才需要采集这道证据。

### 零全局

```text
NM=/Users/nickji/Library/Android/sdk/ndk/30.0.16248370/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-nm
SO=build/app/intermediates/cxx/Debug/12l21261/obj/arm64-v8a/libghostlock43499.so
$NM -S --defined-only "$SO" | awk '$3 ~ /^[bB]$/ {print $4}' | grep -v '^_'
# PASS：输出为空
```
