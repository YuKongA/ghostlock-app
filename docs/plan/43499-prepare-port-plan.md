# 43499 native prepare 端口计划（N3 页发送）

状态：用户已选 A。`NativePrepare43499Port`/JNI/192/160 codec 已实现；AVD 成功 `status=0`、`base=-549752602624`、`shapingFd=82`、`shapingPeerFd=83`，Kotlin 页 16 KiB 经 fd 送出、actor `requeue=1`。仍无内核页读回/uid=0。

> **2026-10-21 指针**：批准状态见 [43499-approval-status.md](43499-approval-status.md)（唯一权威，本件不再自行声明）；
> 执行结构（PipelineEngine 唯一权威 / Stage 与 KtStep 只是 node 形态 / 会话 close 与执行 close 互斥）见
> [execution-architecture.md](execution-architecture.md)。
> 本件端口**已实现**（AVD `status=0`/`shapingFd`）；**页落点未证**。

## 1. 边界

- Kotlin 提供全部 prepare 参数（setup/region/patience/inputs/geometry/range/proofs）；
- C++ handle 持有一个 `WordsRegion`，只调用 `run_prepare`；
- C++ 不读 profile，不建页，不判定页/root；
- Kotlin 用 `base + payload_delta` 建页，调用 `NativeAttemptParams.pageFd` 发送。

## 2. JNI

```text
nativePrepareCreate(): Long
nativePrepareRun(handle, params: ByteArray): ByteArray
nativePrepareProof(handle, index): Long
nativePrepareWord(handle, index): Long
nativePrepareClose(handle): Long
```

- create 分配 handle（含 WordsRegion）；run 只执行一次；close 释放 region/fd；
- `shaping_fd/shaping_peer_fd` 由 result 返回，Kotlin 负责 close；handle close 不得重复关 fd；
- run 成功：fds 转移给 Kotlin，handle close 只释放 WordsRegion；run 失败：handle close 关闭自建 fds；
- `pageFd` 短写/负写一律不启动 actor；`shaping_peer_fd` 必须活到页发送完成；
- proof/word 从 handle 读，供 actor 参数使用。

## 3. Params 最小集

- page_size、mm_struct_size、mm_slab_order、collisions、thread_count、possible_cpus、proof_count、proof_seed；
- `sweep_tags` 必须由 Kotlin 给，C++ 不再硬编码 false；
- range_start/range_end、counter_offset、wake_rounds/round_timeout_us；
- region header/total_pokes/word_bytes/handle_bytes；
- prepare groups: pre/pre_shaped/spray/post/leak；snitch_collisions；
- max_attempts/poll_rounds、socket_buffer/main_cpu。

C++ 用已有 `build_snitch_setup`/`region_for`/`send_size_for` 推导 setup/geometry；Kotlin 不重复实现这些派生公式，但所有输入来自 Kotlin。

## 4. Result

`status`、`at_step`、`base`、`wordsBase`、`payloadBase/payloadOffset`、`shaping_fd`、`shaping_peer_fd`、`opened/closed`、`error`、`snitch_*`、`proof_count/proofs[4]`、`words[4]`。

- `base` 不是 payload 基址；N3 建页用 `payloadBase` 或 `wordsBase + payloadOffset`。
- `page_base` 的来源必须写清：prepare 的 payload 页，或 discover 的明确同一 mapping 结果；禁止把 `found` 自动当页。

- `status` 为具名枚举；`shaping_fd < 0` 即拒绝；
- `base=0` 即失败；Kotlin 不补默认值。

## 5. 数据流

```text
Kotlin discover -> mm_struct
Kotlin prepare -> base + shaping_fd + words
Kotlin build page with payload_base=base+delta
NativeAttempt(pageFd=shaping_fd) -> write(page) -> actor
Kotlin uid -> success
```

## 6. 门禁

- 主机：params/result codec、handle 幂等 close、fd 不重复关；
- AVD：prepare 返回 base/fd，页经 fd 写出，actor requeue=1；
- 真机：冷机、固定 CPU、单 route、uid=0；未过不标 supported。

## 7. 回滚

- port 未注入 -> `ENOSYS`；旧 `KernelPageWriter` fail-closed；
- C++ target 默认 OFF；不删除 legacy；
- 页发送失败不运行 actor。
