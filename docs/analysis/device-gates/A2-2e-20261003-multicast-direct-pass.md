# A2-2e 真机门禁：slab_drain/perf_find_task 迁到 backend primitives（multicast_waiter）— PASS（冷机复跑）

对应提交 `07b8b1e`（Phase A2-2e）。候选二进制 SHA-256
`655d17c41ecb60ad2807e0a080f8ed59128df3122c5b34eede079abbe97037e6`。

## 设备与入口

- A301SO；`5.15.189-android13-8-00016-g51bba4309aac-ab14546557`；route = multicast_waiter；CPU 对 0/1。
- 入口：`GHOSTLOCK_HOME=/data/local/tmp /data/local/tmp/ghostlock-a2f --load-prebuilt-profile
  /data/local/tmp/p0.bin`。

## 结果

**第一次冷机跑**：`Write 1 failed`（与 A2-2b 首跑同类）。日志
`A2-2e-20261003-multicast-rerun-fail.native.log`。

**同构建冷机复跑**：PASS（`.native.log`）：

```
[*] multicast route status=0 clean=1/1 step=0 errno=0 attempts=16 calls=1 success=1   # x4
[+] child is root!
[*] [T+20224ms] exploit complete
[+] KernelSU ready
```

事后：`kernelsu ... (OE)`；`su -c id` → `uid=0(root)`；无 panic。

## 判定

首跑失败为已知竞争抖动（同 A2-2b，属 `KERNEL-PANIC-01` 同类），非本批引入：同构建冷机复跑 PASS；
`cmp_disasm --reviewed` 6 函数骨架一致（`do_one_write` 126）。

## 变更说明

`slab_drain` 与 `perf_find_task` 从 `attack::ops.cpp` 迁到 `backend/cve_2026_43499/primitives.{hpp,cpp}`
（namespace `ghostlock::backend`）。`perf_find_task` 内的 `in_direct_map(v)` 改为内联表达式
`v > memory::DIRECT_MAP_BASE && v < memory::g_direct_map_end`，避免 primitives 依赖 `attack`（行为等价）。
`attack/ops.cpp` 现已为空并删除；`attack/ops.hpp` 暂留 `in_direct_map`（A2-3 再归位 `memory`）。
host 数据流 harness 的 `attack_stub.cpp` 提供 `backend::slab_drain`/`backend::perf_find_task` 空桩。
