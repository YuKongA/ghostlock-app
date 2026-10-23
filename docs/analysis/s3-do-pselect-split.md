# S3 证据：do_pselect_fake_lock_route 拆分（2026-10-21）

> 设计：[s3-do-pselect-design](../analysis/s3-do-pselect-design.md)；技术阻断 0 后实施。

## 拆分

```text
pselect_route_state_init
pselect_route_setup
pselect_route_wait
pselect_route_finish
pselect_route_setup_cleanup
```

`do_pselect_fake_lock_route` 只做：

```text
validate → init → setup → wait → finish
```

## 关键修正

- state 初始化 fd=-1、high_read=-1、stdio_backup=-1。
- `restore_standard_io(const int[3])` 保持原样；新增 `close_standard_io_backups(int[3])`
  在 restore 后 close 备份并置 -1（不再泄漏）。
- `open_selected_fds` 改 `int`；内部 F_DUPFD 失败返回 -1，setup 进入 cleanup。
- setup 失败 cleanup：restore+close stdio，关闭 block/pipe/high_read。
- `errno=0` 在 pselect/select 前；saved_errno 在 restore 前保存。
- consumer_stuck 仍只泄漏 block/pipe，状态 34；stdio 备份已恢复关闭。

## 门禁

```text
./gradlew :app:externalNativeBuildDebug --offline  # BUILD SUCCESSFUL, 0 warnings

important_flatten：
  FLAT-IGNORE close
  FLAT-SUMMARY base=4 cur=4 args_diff=4
  FLAT-RESULT: PASS

攻击顺序：syscall(timerfd) → fcntl(F_DUPFD) → pselect/select。
close 被排除：S3 同时修复 stdio backup 泄漏，close 数量增加。

cmp_disasm --important-only：
  IMPORTANT-REMOVED do_pselect_fake_lock_route 为预期搬迁；
  flatten 已覆盖；其余无 COUNT/ORDER FAIL。

llvm-nm 43499 b/B = 0
```

## 文档

- [S1](s1-run-exploit-split.md)
- [S2](s2-do-tcp-split.md)
- [拆分计划](../plan/split-large-functions.md)
- 日志：`build/analysis/flatten-s3.log`、`build/analysis/important-s3.log`
