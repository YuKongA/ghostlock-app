# 写路径手动 trace（2026-10-17，按代码逐行核对）

## 每块调用链

```text
pagecache_steps.cpp::write_image_verified  （每块一次循环）
  └─ landing/pagecache_write.hpp::write_block(position)          :132
       ├─ splice_.read_at(doors_.file_fd(), old_block, 16, pos)   :141   ← 旧块（preimage）
       │    └─ file_fd<0 ⇒ RealSpliceIo::read_at ⇒ old_page.read16
       │         └─ crash_dump_bridge.cpp::read16                 :45   ⇒ ::fork() :93   ★ fork #1
       ├─ 组帧（IV/ICV）+ splice_one(…, datagram)                  :206
       │    └─ helper 分支 ⇒ crash_dump_bridge::splice16           :60   ⇒ ::fork() :93   ★ fork #2
       └─ splice_.read_at(doors_.file_fd(), block, 16, pos)        :250   ← 回读校验（现已抽样）
```

## 与实测对照

| 项 | 代码预期 | 真机实测（2026-10-17） |
|---|---|---|
| 每块 fork 数 | 2 次 + 1/32 抽样 ≈ **2.06** | 1470 块 / 114 s ⇒ **≈78 ms/块** ≈ **3 次 fork × 25 ms** |
| 769 块 | ≈ 39 s | 57 s（更早一批，未抽样时） |

⇒ **矛盾点**：抽样已落地（`verify_every=32`），但每块仍像 3 次 fork。三个候选原因，按可查顺序：

1. `write_block` 的 :250 回读**不经过** `write_image_verified` 的抽样判据（它在 landing 层，抽样在 steps 层）；
2. 复原路径（`CarrierRestore`）自己调 `writeImage`，其 `verify_every` 未必是默认 32；
3. `splice_one` 内部有一次诊断 `read16`（抽样 `offset % 512 == 0`），在 512 对齐的块上会多一次。

## 下一步的核对方法（离线即可）

- 在 `write_block` 的 :250 前打印一行 `verify_block pos=… sampled=<0|1>`，用 `verify_every` 判据统一决定是否回读；
- 复原路径显式传 `verify_every=0`（它需要每块证据）⇒ 那次 114 s 可由 1470×3 次 fork 完全解释；
- 之后一次真机跑，用 `write benchmark: per_block_us` 判定是否降到 2 次 fork 的水平（≈50 ms/块）。

## 结论

写路径目前**没有**常驻桥，每块最少 2 次 fork；要达到 5 s 量级（1470 块）必须把 fork 降到 ~0（可直读载体）
或改成常驻桥（每次 IPC 代替每次 fork），二者都已在设计稿 `persistent-helper-bridge-design.md` 里给出。
