# S5 证据：prepare_skb_payload 拆分（2026-10-21）

> 设计：[s5-skb-payload-design](../analysis/s5-skb-payload-design.md)；技术阻断 0 后实施。

## 拆分

```text
payload_geometry
payload_init_fake
payload_write_waiter
payload_write_task
```

`prepare_skb_payload` 只做：

```text
memset → geometry → init_fake → for chunk { waiter; task } → 1
```

## 关键保持

- `chunk_bias`/ORDER3_SIZE 步长不变。
- `fake_lock/w0/task/fops/parent/right/left` 仍在循环前赋值一次。
- compact tcp `write_right` 三分支不变。
- `fill_profile_cred_copy` 偏移由 helper 内 `g->tcp` 决定。
- cred copy 失败立即返回 0。
- `post_ctx`/spray 循环保护保留。

## 门禁

```text
./gradlew :app:externalNativeBuildDebug --offline  # BUILD SUCCESSFUL, 0 warnings

important_flatten（4 helper）：
  FLAT-SUMMARY base=0 cur=0 args_diff=0
  FLAT-RESULT: PASS

prepare_skb_payload 无重要 call/syscall；flatten 为结构性判据。

cmp_disasm --important-only：
  仅 IMPORTANT-REMOVED prepare_kernel/do_tcp/do_pselect/run_exploit/futex_init 等预期搬迁；
  无新增 COUNT/ORDER FAIL。

llvm-nm 43499 b/B = 0
```

## 文档

- [S4](s4-prepare-kernel-split.md)
- [拆分计划](../plan/split-large-functions.md)
- 日志：`build/analysis/flatten-s5.log`、`build/analysis/important-s5.log`
