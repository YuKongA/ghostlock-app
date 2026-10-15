# 43499 写落地 · 本轮进展与已定形的修法（2026-10-11 夜）

## 已有的硬结论（都可复核）

1. **判据必须被尊重**（已落码 ✓）：`layout_accepts_page` 在 `Zero + preserve_child` 时要求
   `((value) >> 16) & 1 == 1`；不满足时 main 现在报 **`payload-layout-not-accepted`** ✓
   （此前是**悄悄继续** ✗ —— 这正是"零字节落地"的一类原因 ✓）。
2. 该判据在真机上**确实会触发** ✓：`main: layout matches=1 accepts=0 value=0xffff800000027500 mode=1`
   ⇒ `payload_for_zero_write()` 在 128 KiB 区里**只有两个 64 KiB 候选**，而区的基址只保证页对齐 ⇒
   两个候选可能**都不合格** ✗（本轮即如此 ✓）。

## 已定形的修法（下一步直接做，设计与真值都已定）

- 给载荷**单独映射 1 MiB**（16 个 64 KiB 候选 ⇒ 奇偶里必有合格的 ✓）；
- 在该映射里按 64 KiB 步长扫描，返回第一块满足 `((addr + 0x100) >> 16) & 1 == 1` 的页；
- 两个映射都归 `WordsRegion` 所有（同一套"调用方持有"规则 ✓），`close()` 里一并归还 ✓；
- 取不到合格页 ⇒ **具名拒绝**（`payload-layout-not-accepted` ✓），不退回旧路径 ✓。

## 已就位但尚未用到的探针（登记：同批删除）

- `core/src/backend/cve_2026_43499/landing/multicast_route.hpp:413` 有 `TEMPWAIT waiter ret=… errno=…` ✓；
  本轮因判据先拒（`payload-layout-not-accepted` ✓）而**没跑到** ✗ ⇒ 修完奇偶后第一件事就是读它 ✓；
- 待删清单同时包含 main 里的 `VOTES`/`PF*`/`sentinel`/`layout` 诊断打印 ✓。

## 与归档成功日志的差别（继续有效）

- 归档成功那次：`waiter parked; owner started` ✓、`CMP_REQUEUE_PI ret=-1 errno=35` ✓、`root!`/`uid=0` ✓、
  `target = child_task + 0x798(=1944)` ✓、`mode=2(Credential)` ✓；
- 我这边：`waiter=-1` ✗（等待者没停住）⇒ 先看 `TEMPWAIT` 的 errno ✓，再谈 `mode=Credential` 与节拍旋钮 ✓。
