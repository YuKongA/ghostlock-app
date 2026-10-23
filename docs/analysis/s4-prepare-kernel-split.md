# S4 证据：prepare_kernel_page 拆分（2026-10-21）

> 设计：[s4-prepare-kernel-design](../analysis/s4-prepare-kernel-design.md)；技术阻断 0 后实施。

## 拆分

```text
prepare_reset
prepare_spray_contexts
prepare_leak_children
prepare_wait_collisions
prepare_leak_mm
prepare_payload
prepare_fail
```

`prepare_kernel_page` 只保留单一 `fail:` 出口。

## 关键修正

- `prepare_reset` 检查 `mm_objs_per_slab > 0`；post_ctx 循环加 `mm_cnt > 0`，spray 循环加 step 保护。
- `prepare_spray_contexts` 检查 `kernelsnitch_setup` NULL；失败先 kill spray children。
- `prepare_leak_children` 只负责 child_leak/pre/post/memfd/kill。
- `prepare_payload` 中 `prepare_skb_payload` 失败返回 0，不提前 cleanup。
- `prepare_fail` 只 kill prepare_ctx；不 kill 已 wait 的 child_leak。
- pre/post/spray kill 顺序保持：pre → post → spray。
- post_ctx close 到 `mm_cnt-1`，保留最后一个；spray 每 slab close。

## 门禁

```text
./gradlew :app:externalNativeBuildDebug --offline  # BUILD SUCCESSFUL, 0 warnings

important_flatten（7 helper）：
  FLAT-SUMMARY base=17 cur=17 args_diff=13
  FLAT-RESULT: PASS

args diff：waitpid/kill/setsockopt/fcntl/pin_to_core/close/sendmsg x0；
由 helper 栈帧或 state 地址变化引起。

cmp_disasm --important-only：
  IMPORTANT-REMOVED prepare_kernel_page 为预期搬迁；
  flatten 已覆盖；其余无 COUNT/ORDER FAIL。

llvm-nm 43499 b/B = 0
```

## 文档

- [S1](s1-run-exploit-split.md)
- [S2](s2-do-tcp-split.md)
- [S3](s3-do-pselect-split.md)
- [拆分计划](../plan/split-large-functions.md)
- 日志：`build/analysis/flatten-s4.log`、`build/analysis/important-s4.log`
