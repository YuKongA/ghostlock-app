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

## 复核更正（同日）

上表把 78 ms/块 读成「3 次 fork × 25 ms」，这一步是错的：

- `pagecache_write.hpp:250` 的 `read_block` 是 landing 的**读取 API**，只有 steps 层在抽样判据成立时才会调用它；
  它并不是 `write_block` 内部的固定第三步（先前那段 sed 把两个函数拼在了一起看）。
- 若每次 fork 的真实成本约 **38 ms**，那么 2.06 次/块 × 38 ms ≈ **78 ms/块**，与 1470 块 / 114 s 完全吻合。
  ⇒ 因此**抽样是生效的**，没有「未抽样的第三次 fork」这个缺陷。
- 早期 769 块 / 57 s（≈74 ms/块）发生在抽样落地之前，那时是 3 次/块 ⇒ 3 × 25 ms ≈ 75 ms 也吻合（fork 成本随负载在 25–38 ms 间浮动）。

## 结论（更正后）

写路径每块最少 **2 次 fork**（旧块 + splice 页），这是 vendor 载体的固有成本；
要把 1470 块压到 5 s 量级，只有两条路：**可直读载体**（fork→pread）或**常驻桥**（fork→IPC），
两者都写在 `persistent-helper-bridge-design.md` 里。抽样只影响回读，不改变这两条路径的选择。

## 附：trigger 点火者的对照（同日 trace 第二项）

| 项 | 参照 `5f184760` | 我们 |
|---|---|---|
| 形态 | `real_ops.cpp:359` 双 fork；`:372-373` 孙进程 `usleep(delay)` 后**可选** `execl(exec_path, …)` | `TriggerSpec43284.detonate` 双 fork，孙进程睡 `delayMs` 后按 `igniterPath` 执行 |
| 点火者 | **参数** `ctx->exec_path`（生产由 `execution_binding.cpp:170` 给 500 ms） | **参数** `igniterPath`，默认 `/apex/com.android.runtime/bin/crash_dump32` + args `1 1 11 1` |
| 延迟 | 参数（生产 500） | `DEFAULT_DELAY_MS = 500` |
| 重试 | 无（一次点火） | `DEFAULT_RETRIES = 1`（失败重试一次） |

⇒ 结论：trigger 的结构与默认值**一致**；我们对点火失败多一次重试，属于更保守的差异（不改变成功路径）。

## 附：wait 判据的对照（trace 第三项）

| 项 | 参照 `real_ops.cpp:400-420` | 我们 `WaitPolicy43284.await` | 差异 |
|---|---|---|---|
| 结构 | **单循环**跑满 `kLkmWaitTimeoutMs`（15000） | **两段式**：先 15000 跑主判据，仍在飞再给 200 ms 备用 | 结构不同 |
| 失败优先 | **先判失败**（`kLkmFailureMarker` 与 alt），再判成功 —— 陈旧 success 不会掩盖新的 failure | 先主判据（成功/失败同权，由 probe 返回顺序决定） | **行为可能不同** |
| 备用标记 | 成功/失败的 alt 标记与主标记**在同一个循环里**都判 | alt 只在第二段（200 ms）内判 | 若 alt 在 15 s 后才出现会漏 |
| 模块存在 | `probe_exists(kLkmModulePath)` 作为**成功判据之一** | 我们也有 `moduleMarker`/`moduleName`（自检用到），但攻击段是否把它当成功判据**未在代码中确认** | 待核 |
| `/dev/df` | 明确**不是**终态（只表示钩子被踩到） | 同样只作 inflight | 一致 |

⇒ 待办（离线可改）：把 `await` 改成 ref 的**单循环 + 失败优先 + 主/备标记同轮判 + 模块存在成功**；
  单测覆盖：陈旧 success + 新 failure ⇒ 必须判失败；alt 在 14.9 s 出现 ⇒ 成功；模块存在但无标记 ⇒ 成功。
