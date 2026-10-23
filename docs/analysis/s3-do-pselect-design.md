# S3 设计：do_pselect_fake_lock_route 拆分（2026-10-21）

> 从属 [split-large-functions](../plan/split-large-functions.md) S3。目标：148 行/13 分支 → 小函数；不改 FD_SET、pselect 参数、stdio 备份、close/leak 顺序。

## 状态

```c
struct pselect_route_state {
  int pipefd[2];
  int block_fd;
  int high_read;
  int compact_route;
  int delay_usec;
  int stdio_backup[3];
  fd_set in, out, ex;
  struct timespec t0;
  int ret;
  int saved_errno;
  int calls, success;
  int consumer_stuck;
};
```

状态只在 `do_pselect_fake_lock_route` 栈；不进入 session。

```c
static void pselect_route_state_init(struct pselect_route_state *st);
/* pipefd[] = block_fd = high_read = -1; stdio_backup[] = -1;
   ret/saved_errno/calls/success/consumer_stuck = 0; */
```

## 拆分

```c
static int pselect_route_setup(Glk43499Session *s, const GlkWriteRequest *req,
                               struct pselect_route_state *st);
static void pselect_route_wait(Glk43499Session *s,
                               struct pselect_route_state *st);
static void pselect_route_finish(Glk43499Session *s,
                                 struct pselect_route_state *st);
static void pselect_route_setup_cleanup(struct pselect_route_state *st);
```

### setup

1. `pipe(pipefd)`（SYSCHK，保持）
2. `compact_route = t->compact_waiter`
3. `timerfd_create`；失败 → `block_fd=pipefd[0]` + warning
4. `fcntl(F_DUPFD, PSELECT_ROUTE_NFDS+16)`；失败 → step31/errno，关闭 block（若非 pipe）与 pipe，return -1
5. `prepare_pselect_fdsets`
6. setup 日志（shift/page/fake_*/in/out/ex words）
7. `reserve_standard_io(stdio_backup)`
8. `open_selected_fds(...)` 改为 `int`：内部 `F_DUPFD` 失败返回 -1；
   setup 捕获后 step31/errno，执行 `pselect_route_setup_cleanup`（restore+close stdio，
   关闭 block/pipe/high_read）并返回 -1。成功则 close high_read。
9. reset consumer_calls/success、punch_consume_stop
10. `delay_usec=route_delay_usec(1)`；`main_route_delay_usec=delay`；`punch_consume_go=1`
11. pre-select 日志

### wait

1. **先 `errno=0`**
2. compact → `pselect(PSELECT_ROUTE_NFDS,...,&ts,NULL)`
3. 否则 `select(...,&timeval)`
4. 立即 `saved_errno=errno`
5. `restore_standard_io(const int[3])` 保持签名；
   随后 `close_standard_io_backups(int[3])` 关闭备份并置 -1。
   setup_cleanup 与 wait 都调用 restore + close 两个步骤。
5. post-select 日志；`punch_consume_go=0`
6. 若 `consumer_inflight`：最多 2000×1ms 等待，置 `consumer_stuck`
7. 读 calls/success，log

### finish

1. calls>0 && success>0 → step/errno=0；否则 step33 + saved_errno
2. consumer_stuck → step34 + error，**不 close**（保持 leak-on-stuck）
3. 否则 close block（若非 pipe）、pipe0、pipe1
4. 最终 done 日志

## do_pselect 编排

```text
validate page/fake_lock/fake_fops
state init
setup 失败 → setup_cleanup → return
wait
finish
```

## 所有权 / 失败路径

| 对象 | owner | 失败 | 成功 |
|---|---|---|---|
| pipefd | state | setup 失败 close | finish close |
| block_fd | state | setup 失败 close（非 pipe） | finish close/leak |
| high_read | setup 局部 | setup 失败 close | open 后立即 close |
| stdio backup | state | setup cleanup restore+close | wait restore+close |
| consumer_stuck | state | — | 故意 leak |

- setup 失败：high_read 或 open_selected_fds 失败；cleanup 需要 restore+close stdio
  （若已 reserve），再 close block（非 pipe）/pipe0/1/high_read。
- wait 之后任何路径都必须 finish；不得提前 return。
- leak-on-stuck 的 TODO/temp 注释保留。

## 风险

- FD_SET 必须在 setup 后、open 前完成。
- `restore_standard_io` 必须在 select 返回后立即执行，早于任何日志/等待；
  紧接着 `close_standard_io_backups` 关闭备份并置 -1，不再泄漏。
- `errno=0` 必须在 select 前；`saved_errno` 在 restore 前保存。
- close 顺序 block→pipe0→pipe1 不变。
- `consumer_stuck` 时 block/pipe 不 close，状态 34。

## 门禁

```text
build 0 warnings
python3 tools/important_flatten.py \
  build/native/baseline-20261021/libghostlock43499.so \
  build/app/intermediates/cxx/Debug/12l21261/obj/arm64-v8a/libghostlock43499.so \
  --parent do_pselect_fake_lock_route \
  --helper pselect_route_setup --helper pselect_route_wait \
  --helper pselect_route_finish --helper pselect_route_setup_cleanup \
  --ignore-symbol close
# COUNT/ORDER PASS；args_diff 仅报告。
# close 被排除：S3 同时修复 stdio backup 泄漏（restore 后 close），
# close 数量增加；攻击顺序判据是 syscall → fcntl → pselect/select。
cmp_disasm --important-only：无新增 COUNT/ORDER FAIL
llvm-nm b/B=0
```
